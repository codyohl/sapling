"use strict";(self.webpackChunkwebsite=self.webpackChunkwebsite||[]).push([[8953],{3905:(e,t,n)=>{n.r(t),n.d(t,{MDXContext:()=>l,MDXProvider:()=>d,mdx:()=>g,useMDXComponents:()=>u,withMDXComponents:()=>m});var a=n(67294);function r(e,t,n){return t in e?Object.defineProperty(e,t,{value:n,enumerable:!0,configurable:!0,writable:!0}):e[t]=n,e}function i(){return i=Object.assign||function(e){for(var t=1;t<arguments.length;t++){var n=arguments[t];for(var a in n)Object.prototype.hasOwnProperty.call(n,a)&&(e[a]=n[a])}return e},i.apply(this,arguments)}function o(e,t){var n=Object.keys(e);if(Object.getOwnPropertySymbols){var a=Object.getOwnPropertySymbols(e);t&&(a=a.filter((function(t){return Object.getOwnPropertyDescriptor(e,t).enumerable}))),n.push.apply(n,a)}return n}function s(e){for(var t=1;t<arguments.length;t++){var n=null!=arguments[t]?arguments[t]:{};t%2?o(Object(n),!0).forEach((function(t){r(e,t,n[t])})):Object.getOwnPropertyDescriptors?Object.defineProperties(e,Object.getOwnPropertyDescriptors(n)):o(Object(n)).forEach((function(t){Object.defineProperty(e,t,Object.getOwnPropertyDescriptor(n,t))}))}return e}function c(e,t){if(null==e)return{};var n,a,r=function(e,t){if(null==e)return{};var n,a,r={},i=Object.keys(e);for(a=0;a<i.length;a++)n=i[a],t.indexOf(n)>=0||(r[n]=e[n]);return r}(e,t);if(Object.getOwnPropertySymbols){var i=Object.getOwnPropertySymbols(e);for(a=0;a<i.length;a++)n=i[a],t.indexOf(n)>=0||Object.prototype.propertyIsEnumerable.call(e,n)&&(r[n]=e[n])}return r}var l=a.createContext({}),m=function(e){return function(t){var n=u(t.components);return a.createElement(e,i({},t,{components:n}))}},u=function(e){var t=a.useContext(l),n=t;return e&&(n="function"==typeof e?e(t):s(s({},t),e)),n},d=function(e){var t=u(e.components);return a.createElement(l.Provider,{value:t},e.children)},p={inlineCode:"code",wrapper:function(e){var t=e.children;return a.createElement(a.Fragment,{},t)}},h=a.forwardRef((function(e,t){var n=e.components,r=e.mdxType,i=e.originalType,o=e.parentName,l=c(e,["components","mdxType","originalType","parentName"]),m=u(n),d=r,h=m["".concat(o,".").concat(d)]||m[d]||p[d]||i;return n?a.createElement(h,s(s({ref:t},l),{},{components:n})):a.createElement(h,s({ref:t},l))}));function g(e,t){var n=arguments,r=t&&t.mdxType;if("string"==typeof e||r){var i=n.length,o=new Array(i);o[0]=h;var s={};for(var c in t)hasOwnProperty.call(t,c)&&(s[c]=t[c]);s.originalType=e,s.mdxType="string"==typeof e?e:r,o[1]=s;for(var l=2;l<i;l++)o[l]=n[l];return a.createElement.apply(null,o)}return a.createElement.apply(null,n)}h.displayName="MDXCreateElement"},920:(e,t,n)=>{n.d(t,{RJ:()=>l,Xj:()=>s,bv:()=>c,mY:()=>o,nk:()=>m});var a=n(67294),r=n(44996),i=n(50941);function o(e){let{name:t,linkText:n}=e;const i=function(e){switch(e){case"go":return"goto";case"isl":return"web"}return e}(t),o=null!=n?n:t;return a.createElement("a",{href:(0,r.default)("/docs/commands/"+i)},a.createElement("code",null,o))}function s(e){let{name:t}=e;return a.createElement(o,{name:t,linkText:"sl "+t})}function c(){return a.createElement("p",{style:{textAlign:"center"}},a.createElement("img",{src:(0,r.default)("/img/reviewstack-demo.gif"),width:800,align:"center"}))}function l(e){let{alt:t,light:n,dark:o}=e;return a.createElement(i.Z,{alt:t,sources:{light:(0,r.default)(n),dark:(0,r.default)(o)}})}function m(e){let{src:t}=e;return a.createElement("video",{controls:!0},a.createElement("source",{src:(0,r.default)(t)}))}},88930:(e,t,n)=>{n.r(t),n.d(t,{assets:()=>l,contentTitle:()=>s,default:()=>d,frontMatter:()=>o,metadata:()=>c,toc:()=>m});var a=n(83117),r=(n(67294),n(3905)),i=n(920);const o={sidebar_position:3},s="ghstack",c={unversionedId:"git/ghstack",id:"git/ghstack",title:"ghstack",description:"ghstack (https://github.com/ezyang/ghstack) is a third-party tool designed to facilitate a stacked diff workflow in GitHub repositories by creating a separate pull request for each commit in a stack. To achieve this, it creates a number of synthetic branches under the hood so that each pull request is scoped to the diff for an individual commit.",source:"@site/docs/git/ghstack.md",sourceDirName:"git",slug:"/git/ghstack",permalink:"/docs/git/ghstack",draft:!1,editUrl:"https://github.com/facebookexperimental/eden/tree/main/website/docs/git/ghstack.md",tags:[],version:"current",sidebarPosition:3,frontMatter:{sidebar_position:3},sidebar:"tutorialSidebar",previous:{title:"Sapling stack",permalink:"/docs/git/sapling-stack"},next:{title:"Signing Commits",permalink:"/docs/git/signing"}},l={},m=[],u={toc:m};function d(e){let{components:t,...n}=e;return(0,r.mdx)("wrapper",(0,a.Z)({},u,n,{components:t,mdxType:"MDXLayout"}),(0,r.mdx)("h1",{id:"ghstack"},"ghstack"),(0,r.mdx)("p",null,"ghstack (",(0,r.mdx)("a",{parentName:"p",href:"https://github.com/ezyang/ghstack"},"https://github.com/ezyang/ghstack"),") is a third-party tool designed to facilitate a stacked diff workflow in GitHub repositories by creating a separate pull request for each commit in a stack. To achieve this, it creates a number of synthetic branches under the hood so that each pull request is scoped to the diff for an individual commit."),(0,r.mdx)("p",null,"Sapling includes a custom version of ghstack via its builtin ",(0,r.mdx)(i.Xj,{name:"ghstack",mdxType:"SLCommand"})," subcommand. It uses the same branching strategy as stock ghstack, so it is possible to publish a stack in Sapling using ",(0,r.mdx)(i.Xj,{name:"ghstack",mdxType:"SLCommand"})," and then import it into a Git working tree of the same repository using stock ghstack (or vice versa)."),(0,r.mdx)("p",null,"If you are not familiar with ghstack, be aware of the following limitations:"),(0,r.mdx)("admonition",{type:"caution"},(0,r.mdx)("ol",{parentName:"admonition"},(0,r.mdx)("li",{parentName:"ol"},(0,r.mdx)("inlineCode",{parentName:"li"},"sl ghstack")," requires having ",(0,r.mdx)("em",{parentName:"li"},"write")," access to the GitHub repo that you cloned. If you do not have write access, consider using ",(0,r.mdx)("a",{parentName:"li",href:"/docs/git/sapling-stack"},"Sapling Stacks")," instead."),(0,r.mdx)("li",{parentName:"ol"},"You will NOT be able to merge these pull requests using the normal GitHub UI, as their base branches will not be ",(0,r.mdx)("inlineCode",{parentName:"li"},"main")," (or whatever the default branch of your repository is). Instead, lands must be done via the command line: ",(0,r.mdx)("inlineCode",{parentName:"li"},"sl ghstack land $PR_URL"),"."))),(0,r.mdx)("p",null,"Further, note that Sapling's version of ghstack takes a different approach to configuration and authorization than stock ghstack. Specifically, ",(0,r.mdx)("strong",{parentName:"p"},"it does not rely on a ",(0,r.mdx)("inlineCode",{parentName:"strong"},"~/.ghstackrc")," file"),". So long as you have configured the GitHub CLI as described in ",(0,r.mdx)("a",{parentName:"p",href:"/docs/introduction/getting-started#authenticating-with-github"},"Getting Started"),", you can start using ",(0,r.mdx)(i.Xj,{name:"ghstack",mdxType:"SLCommand"})," directly."),(0,r.mdx)("p",null,"Once you have a stack of commits, you can use ",(0,r.mdx)(i.Xj,{name:"ghstack",mdxType:"SLCommand"})," to create a pull request for each commit in the stack, or to update existing pull requests linked to the commits."),(0,r.mdx)("p",null,"See the help text for the ",(0,r.mdx)(i.mY,{name:"ghstack",mdxType:"Command"})," command for more details."))}d.isMDXComponent=!0}}]);