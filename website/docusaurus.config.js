// @ts-check
import {themes as prismThemes} from 'prism-react-renderer';

/** @type {import('@docusaurus/types').Config} */
const config = {
  title: 'Massif Maps',
  tagline: 'Open, multi-platform maps & location services for Android and iOS — desktop and web next',
  // ?v=3 busts the browser's favicon cache — it keys on the URL, not the bytes.
  favicon: 'img/favicon.svg?v=3',

  url: 'https://massif-maps.github.io',
  baseUrl: '/MassifMaps/',

  organizationName: 'massif-maps',
  projectName: 'MassifMaps',
  deploymentBranch: 'gh-pages',
  trailingSlash: false,

  onBrokenLinks: 'warn',

  markdown: {
    // Parse .md as CommonMark (safe for vendored guides), .mdx as MDX.
    // Mermaid diagrams therefore only render in .mdx pages.
    format: 'detect',
    mermaid: true,
    hooks: {onBrokenMarkdownLinks: 'warn'},
  },
  themes: ['@docusaurus/theme-mermaid'],

  i18n: {
    defaultLocale: 'en',
    locales: ['en'],
  },

  presets: [
    [
      'classic',
      /** @type {import('@docusaurus/preset-classic').Options} */
      ({
        docs: {
          // Single source of truth: the repo-root docs/ tree, browsable on GitHub and
          // published here. See docs/contributing/docs-site.md.
          path: '../docs',
          exclude: ['_archive/**'],
          sidebarPath: './sidebars.js',
          routeBasePath: 'docs',
          // docsDirPath is relative to the site dir ('../docs'), so build the URL
          // from the repo-root path instead of letting Docusaurus concatenate it.
          editUrl: ({docPath}) =>
            `https://github.com/massif-maps/MassifMaps/edit/master/docs/${docPath}`,
          showLastUpdateTime: true,
        },
        blog: false,
        theme: {
          customCss: './src/css/custom.css',
        },
      }),
    ],
  ],

  plugins: [
    // /roadmap is built from the `roadmap`-labelled GitHub issues (see the plugin header).
    './plugins/roadmap-issues',
    // /changelog renders the repo's CHANGELOG.md.
    './plugins/changelog-content',
    [
      '@easyops-cn/docusaurus-search-local',
      /** @type {import('@easyops-cn/docusaurus-search-local').PluginOptions} */
      ({
        hashed: true,
        indexBlog: false,
        docsDir: '../docs',
        docsRouteBasePath: '/docs',
        highlightSearchTermsOnTargetPage: true,
      }),
    ],
  ],

  themeConfig:
    /** @type {import('@docusaurus/preset-classic').ThemeConfig} */
    ({
      // PNG, not the SVG beside it: Twitter, Slack and LinkedIn do not rasterise an SVG og:image.
      image: 'img/social-card.png',
      colorMode: {
        defaultMode: 'light',
        respectPrefersColorScheme: true,
      },
      navbar: {
        title: 'Massif Maps',
        logo: {
          alt: 'Massif Maps',
          src: 'img/logo.svg',
        },
        items: [
          {
            type: 'docSidebar',
            sidebarId: 'docsSidebar',
            position: 'left',
            label: 'Documentation',
          },
          {to: '/examples', label: 'Examples', position: 'left'},
          {to: '/docs/features/3d-terrain', label: 'Features', position: 'left'},
          {to: '/docs/internals/', label: 'Internals', position: 'left'},
          {to: '/platforms', label: 'Platforms', position: 'left'},
          {to: '/roadmap', label: 'Roadmap', position: 'left'},
          {
            label: 'Project',
            position: 'left',
            items: [
              {to: '/changelog', label: 'Changelog'},
              {to: '/integrations', label: 'Integrations'},
              {to: '/community', label: 'Community'},
              {to: '/sponsors', label: 'Sponsors'},
            ],
          },
          {
            label: 'API Reference',
            position: 'left',
            items: [
              {label: 'Android (Javadoc)', href: 'pathname:///MassifMaps/api/android/'},
              {label: 'iOS (Jazzy)', href: 'pathname:///MassifMaps/api/ios/'},
            ],
          },
          {
            href: 'https://github.com/massif-maps/MassifMaps/releases',
            label: 'Releases',
            position: 'right',
          },
          {
            href: 'https://github.com/massif-maps/MassifMaps',
            label: 'GitHub',
            position: 'right',
          },
        ],
      },
      footer: {
        style: 'dark',
        links: [
          {
            title: 'Docs',
            items: [
              {label: 'Getting Started', to: '/docs/getting-started/installation'},
              {label: 'Guides', to: '/docs/guides/map-view'},
              {label: 'Examples', to: '/examples'},
              {label: 'Features', to: '/docs/features/3d-terrain'},
              {label: 'Internals', to: '/docs/internals/'},
              {label: 'Maintenance', to: '/docs/maintenance/'},
              {label: 'Migration', to: '/docs/migration'},
            ],
          },
          {
            title: 'API',
            items: [
              {label: 'Android API', href: 'pathname:///MassifMaps/api/android/'},
              {label: 'iOS API', href: 'pathname:///MassifMaps/api/ios/'},
            ],
          },
          {
            title: 'Project',
            items: [
              {label: 'Platforms', to: '/platforms'},
              {label: 'Roadmap', to: '/roadmap'},
              {label: 'Integrations', to: '/integrations'},
              {label: 'Sponsors', to: '/sponsors'},
            ],
          },
          {
            title: 'More',
            items: [
              {label: 'Changelog', to: '/changelog'},
              {label: 'Community', to: '/community'},
              {label: 'GitHub', href: 'https://github.com/massif-maps/MassifMaps'},
              {label: 'Releases', href: 'https://github.com/massif-maps/MassifMaps/releases'},
              {label: 'Original CARTO docs', href: 'https://cartodb.github.io/developers/mobile-sdk/'},
            ],
          },
        ],
        copyright: `Copyright © ${new Date().getFullYear()} Massif Maps — maintained fork of CartoDB/mobile-sdk. Built with Docusaurus.`,
      },
      prism: {
        theme: prismThemes.github,
        darkTheme: prismThemes.dracula,
        additionalLanguages: ['java', 'kotlin', 'swift', 'objectivec', 'csharp', 'groovy', 'json', 'css', 'bash'],
      },
    }),
};

export default config;
