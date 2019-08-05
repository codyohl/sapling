// Copyright (c) 2004-present, Facebook, Inc.
// All Rights Reserved.
//
// This software may be used and distributed according to the terms of the
// GNU General Public License version 2 or any later version.

#![deny(warnings)]
#![feature(never_type)]

mod dummy;
mod healer;

use blobstore::Blobstore;
use blobstore_sync_queue::{BlobstoreSyncQueue, SqlBlobstoreSyncQueue, SqlConstructors};
use clap::{value_t, App};
use cloned::cloned;
use cmdlib::args;
use context::CoreContext;
use dummy::{DummyBlobstore, DummyBlobstoreSyncQueue};
use failure_ext::{bail_msg, err_msg, prelude::*};
use futures::{
    future::{join_all, loop_fn, ok, Loop},
    prelude::*,
};
use futures_ext::{spawn_future, BoxFuture, FutureExt};
use glusterblob::Glusterblob;
use healer::Healer;
use manifoldblob::ThriftManifoldBlob;
use metaconfig_types::{BlobConfig, MetadataDBConfig, StorageConfig};
use prefixblob::PrefixBlobstore;
use slog::{error, info, o, Logger};
use sql::{myrouter, Connection};
use sqlblob::Sqlblob;
use std::collections::HashMap;
use std::sync::Arc;
use std::time::{Duration, Instant};
use tokio_timer::Delay;

const MAX_ALLOWED_REPLICATION_LAG_SECS: usize = 5;

fn maybe_schedule_healer_for_storage(
    dry_run: bool,
    blobstore_sync_queue_limit: usize,
    logger: Logger,
    storage_config: StorageConfig,
    myrouter_port: u16,
    replication_lag_db_regions: Vec<String>,
    source_blobstore_key: Option<String>,
) -> Result<BoxFuture<(), Error>> {
    let (db_address, blobstores_args) = match &storage_config {
        StorageConfig {
            dbconfig: MetadataDBConfig::Mysql { db_address, .. },
            blobstore: BlobConfig::Multiplexed { blobstores, .. },
        } => (db_address.clone(), blobstores.clone()),
        _ => bail_msg!("Repo doesn't use Multiplexed blobstore"),
    };

    let blobstores: HashMap<_, BoxFuture<Arc<dyn Blobstore + 'static>, _>> = {
        let mut blobstores = HashMap::new();
        for (id, args) in blobstores_args {
            match args {
                BlobConfig::Manifold { bucket, prefix } => {
                    let blobstore = ThriftManifoldBlob::new(bucket)
                        .chain_err("While opening ThriftManifoldBlob")?;
                    let blobstore = PrefixBlobstore::new(blobstore, format!("flat/{}", prefix));
                    let blobstore: Arc<dyn Blobstore> = Arc::new(blobstore);
                    blobstores.insert(id, ok(blobstore).boxify());
                }
                BlobConfig::Gluster {
                    tier,
                    export,
                    basepath,
                } => {
                    let blobstore = Glusterblob::with_smc(tier, export, basepath)
                        .map(|blobstore| -> Arc<dyn Blobstore> { Arc::new(blobstore) })
                        .boxify();
                    blobstores.insert(id, blobstore);
                }
                BlobConfig::Mysql {
                    shard_map,
                    shard_num,
                } => {
                    let blobstore = Sqlblob::with_myrouter(shard_map, myrouter_port, shard_num)
                        .map(|blobstore| -> Arc<dyn Blobstore> { Arc::new(blobstore) });
                    blobstores.insert(id, blobstore.boxify());
                }
                unsupported => bail_msg!("Unsupported blobstore type {:?}", unsupported),
            }
        }

        if !dry_run {
            blobstores
        } else {
            blobstores
                .into_iter()
                .map(|(id, blobstore)| {
                    let logger = logger.new(o!("blobstore" => format!("{:?}", id)));
                    let blobstore = blobstore
                        .map(move |blobstore| -> Arc<dyn Blobstore> {
                            Arc::new(DummyBlobstore::new(blobstore, logger))
                        })
                        .boxify();
                    (id, blobstore)
                })
                .collect()
        }
    };
    let blobstores = join_all(
        blobstores
            .into_iter()
            .map(|(key, value)| value.map(move |value| (key, value))),
    )
    .map(|blobstores| blobstores.into_iter().collect::<HashMap<_, _>>());

    let sync_queue: Arc<dyn BlobstoreSyncQueue> = {
        let sync_queue = SqlBlobstoreSyncQueue::with_myrouter(db_address.clone(), myrouter_port);

        if !dry_run {
            Arc::new(sync_queue)
        } else {
            let logger = logger.new(o!("sync_queue" => ""));
            Arc::new(DummyBlobstoreSyncQueue::new(sync_queue, logger))
        }
    };

    let mut replication_lag_db_conns = vec![];
    let mut conn_builder = Connection::myrouter_builder();
    conn_builder
        .service_type(myrouter::ServiceType::SLAVE)
        .locality(myrouter::DbLocality::EXPLICIT)
        .tier(db_address.clone(), None)
        .port(myrouter_port);

    for region in replication_lag_db_regions {
        conn_builder.explicit_region(region);
        replication_lag_db_conns.push(conn_builder.build_read_only());
    }

    let heal = blobstores.and_then(
        move |blobstores: HashMap<_, Arc<dyn Blobstore + 'static>>| {
            let repo_healer = Healer::new(
                logger.clone(),
                blobstore_sync_queue_limit,
                sync_queue,
                Arc::new(blobstores),
                source_blobstore_key,
            );

            if dry_run {
                let ctx = CoreContext::new_with_logger(logger);
                repo_healer.heal(ctx).boxify()
            } else {
                schedule_everlasting_healing(logger, repo_healer, replication_lag_db_conns)
            }
        },
    );
    Ok(myrouter::wait_for_myrouter(myrouter_port, db_address)
        .and_then(|_| heal)
        .boxify())
}

fn schedule_everlasting_healing(
    logger: Logger,
    repo_healer: Healer,
    replication_lag_db_conns: Vec<Connection>,
) -> BoxFuture<(), Error> {
    let replication_lag_db_conns = Arc::new(replication_lag_db_conns);

    let fut = loop_fn((), move |()| {
        let ctx = CoreContext::new_with_logger(logger.clone());

        cloned!(logger, replication_lag_db_conns);
        repo_healer.heal(ctx).and_then(move |()| {
            ensure_small_db_replication_lag(logger, replication_lag_db_conns)
                .map(|()| Loop::Continue(()))
        })
    });

    spawn_future(fut).boxify()
}

fn ensure_small_db_replication_lag(
    logger: Logger,
    replication_lag_db_conns: Arc<Vec<Connection>>,
) -> impl Future<Item = (), Error = Error> {
    // Make sure we've slept at least once before continuing
    let already_slept = false;

    loop_fn(already_slept, move |already_slept| {
        // Check what max replication lag on replicas, and sleep for that long.
        // This is done in order to avoid overloading the db.
        let mut lag_secs_futs = vec![];
        for conn in replication_lag_db_conns.iter() {
            let f = conn.show_replica_lag_secs().and_then(|maybe_secs| {
                maybe_secs.ok_or(err_msg(
                    "Could not fetch db replication lag. Failing to avoid overloading db",
                ))
            });
            lag_secs_futs.push(f);
        }
        cloned!(logger);

        join_all(lag_secs_futs).and_then(move |lags| {
            let max_lag = lags.into_iter().max().unwrap_or(0);
            info!(logger, "Replication lag is {} secs", max_lag);
            if max_lag < MAX_ALLOWED_REPLICATION_LAG_SECS && already_slept {
                ok(Loop::Break(())).left_future()
            } else {
                let max_lag = Duration::from_secs(max_lag as u64);

                let start = Instant::now();
                let next_iter_deadline = start + max_lag;

                Delay::new(next_iter_deadline)
                    .map(|()| Loop::Continue(true))
                    .from_err()
                    .right_future()
            }
        })
    })
}

fn setup_app<'a, 'b>() -> App<'a, 'b> {
    let app = args::MononokeApp {
        safe_writes: true,
        hide_advanced_args: false,
        default_glog: true,
    };
    app.build("blobstore healer job")
        .version("0.0.0")
        .about("Monitors blobstore_sync_queue to heal blobstores with missing data")
        .args_from_usage(
            r#"
            --sync-queue-limit=[LIMIT] 'set limit for how many queue entries to process'
            --dry-run 'performs a single healing and prints what would it do without doing it'
            --db-regions=[REGIONS] 'comma-separated list of db regions where db replication lag is monitored'
            --storage-id=[STORAGE_ID] 'id of storage group to be healed, e.g. manifold_xdb_multiplex'
            --blobstore-key-like=[BLOBSTORE_KEY] 'Optional source blobstore key in SQL LIKE format, e.g. repo0138.hgmanifest%'
        "#,
        )
}

fn main() -> Result<()> {
    let matches = setup_app().get_matches();

    let storage_id = matches
        .value_of("storage-id")
        .ok_or(err_msg("Missing storage-id"))?;
    let logger = args::get_logger(&matches);
    let myrouter_port =
        args::parse_myrouter_port(&matches).ok_or(err_msg("Missing --myrouter-port"))?;
    let storage_config = args::read_storage_configs(&matches)?
        .remove(storage_id)
        .ok_or(err_msg(format!("Storage id `{}` not found", storage_id)))?;
    let source_blobstore_key = matches.value_of("blobstore-key-like");
    let blobstore_sync_queue_limit = value_t!(matches, "sync-queue-limit", usize).unwrap_or(10000);
    let dry_run = matches.is_present("dry-run");

    let healer = {
        let logger = logger.new(o!(
            "storage" => format!("{:#?}", storage_config),
        ));

        let scheduled = maybe_schedule_healer_for_storage(
            dry_run,
            blobstore_sync_queue_limit,
            logger.clone(),
            storage_config,
            myrouter_port,
            matches
                .value_of("db-regions")
                .unwrap_or("")
                .split(',')
                .map(|s| s.to_string())
                .collect(),
            source_blobstore_key.map(|s| s.to_string()),
        );

        match scheduled {
            Err(err) => {
                error!(logger, "Did not schedule, because of: {:#?}", err);
                return Err(err);
            }
            Ok(scheduled) => {
                info!(logger, "Successfully scheduled");
                scheduled
            }
        }
    };

    let mut runtime = tokio::runtime::Runtime::new()?;
    let result = runtime.block_on(healer.map(|_| ()));
    runtime.shutdown_on_idle();
    result
}
