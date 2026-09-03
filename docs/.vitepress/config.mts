import { resolve } from 'node:path'
import { defineConfig } from 'vitepress'

type DocumentationVersion = {
  label: string
  url: string
}

function documentationVersions(): DocumentationVersion[] {
  const encoded = process.env.CORELIB_DOCS_VERSIONS

  if (encoded === undefined) {
    return []
  }

  const parsed: unknown = JSON.parse(encoded)
  if (!Array.isArray(parsed)) {
    throw new Error('CORELIB_DOCS_VERSIONS must be a JSON array')
  }

  return parsed.map((entry: unknown) => {
    if (
      typeof entry !== 'object' ||
      entry === null ||
      !('label' in entry) ||
      !('url' in entry) ||
      typeof entry.label !== 'string' ||
      typeof entry.url !== 'string'
    ) {
      throw new Error('CORELIB_DOCS_VERSIONS contains an invalid entry')
    }

    return { label: entry.label, url: entry.url }
  })
}

const currentVersion = process.env.CORELIB_DOCS_VERSION ?? 'Development'
const sourceRef = process.env.CORELIB_DOCS_SOURCE_REF ?? 'main'
const outputDirectory = resolve(
  process.cwd(),
  process.env.CORELIB_DOCS_OUT_DIR ?? 'build/docs/site'
)
const versions = documentationVersions()
const versionNavigation =
  versions.length === 0
    ? []
    : [
        {
          text: currentVersion,
          items: versions.map((version) => ({
            text: version.label,
            link: version.url,
            target: '_self',
            noIcon: true
          }))
        }
      ]
const sourceLink =
  sourceRef === 'main'
    ? {
        pattern: 'https://github.com/epicecu/corelib/edit/main/docs/:path',
        text: 'Edit this page on GitHub'
      }
    : {
        pattern: `https://github.com/epicecu/corelib/blob/${sourceRef}/docs/:path`,
        text: 'View source for this version'
      }

export default defineConfig({
  outDir: outputDirectory,
  title: 'Corelib',
  titleTemplate: ':title | EpicECU Corelib',
  description: 'Portable, heap-free middleware for Programmor-compatible embedded devices',
  lang: 'en-AU',
  cleanUrls: true,
  lastUpdated: true,
  head: [['meta', { name: 'theme-color', content: '#ef5b2a' }]],
  themeConfig: {
    logo: '/corelib-logo.png',
    siteTitle: 'Corelib',
    search: { provider: 'local' },
    nav: [
      { text: 'Guide', link: '/introduction' },
      { text: 'C API', link: '/reference/c/' },
      { text: 'C++ API', link: '/reference/cpp/' },
      { text: 'Arduino', link: '/arduino' },
      { text: 'Examples', link: '/examples' },
      ...versionNavigation,
      { text: 'GitHub', link: 'https://github.com/epicecu/corelib' }
    ],
    sidebar: [
      {
        text: 'Guide',
        items: [
          { text: 'Introduction', link: '/introduction' },
          { text: 'Installation', link: '/installation' },
          { text: 'Architecture', link: '/architecture' },
          { text: 'Transactions', link: '/transactions' },
          { text: 'Transport and scheduling', link: '/transport' },
          { text: 'Storage and capacity', link: '/storage' },
          { text: 'Compatibility', link: '/compatibility' },
          { text: 'Troubleshooting', link: '/troubleshooting' }
        ]
      },
      {
        text: 'C integration',
        collapsed: false,
        items: [
          { text: 'Device', link: '/device' },
          { text: 'Gateway', link: '/gateway' }
        ]
      },
      {
        text: 'C++ and Arduino',
        collapsed: false,
        items: [
          { text: 'C++ facade', link: '/cpp' },
          { text: 'Arduino', link: '/arduino' },
          { text: 'Examples', link: '/examples' }
        ]
      },
      {
        text: 'C API',
        collapsed: false,
        items: [
          { text: 'Overview', link: '/reference/c/' },
          { text: 'Constants and types', link: '/reference/c/types' },
          { text: 'Device API', link: '/reference/c/device' },
          { text: 'Gateway API', link: '/reference/c/gateway' }
        ]
      },
      {
        text: 'C++ API',
        collapsed: false,
        items: [
          { text: 'Overview', link: '/reference/cpp/' },
          { text: 'Common types', link: '/reference/cpp/types' },
          { text: 'Handler and Device', link: '/reference/cpp/device' },
          { text: 'Gateway', link: '/reference/cpp/gateway' }
        ]
      },
      {
        text: 'Project',
        items: [
          { text: 'MISRA analysis', link: '/misra' },
          { text: 'Source style', link: '/style' }
        ]
      }
    ],
    editLink: sourceLink,
    socialLinks: [{ icon: 'github', link: 'https://github.com/epicecu/corelib' }],
    footer: {
      message: 'Released under the MIT Licence.',
      copyright: 'Copyright © 2026 EpicECU Pty Ltd'
    }
  }
})
