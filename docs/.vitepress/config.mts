import { defineConfig } from 'vitepress'

const SITE_URL = 'https://hyper.bentopdf.com'

export default defineConfig({
    title: "Hyper Compress Docs",
    description: "Documentation for Hyper Compress - the high fidelity, content preserving PDF compression engine by BentoPDF",
    base: '/docs/',
    cleanUrls: true,
    srcExclude: ['npm-README.md'],

    transformPageData(pageData) {
        const relPath = pageData.relativePath.replace(/\.md$/, '')
        const slug = relPath === 'index' ? '' : relPath.replace(/\/index$/, '/')
        const canonicalUrl = slug ? `${SITE_URL}/docs/${slug}` : `${SITE_URL}/docs/`
        pageData.frontmatter.head ??= []
        pageData.frontmatter.head.push(['link', { rel: 'canonical', href: canonicalUrl }])
    },

    themeConfig: {
        logo: '/images/logo.svg',

        nav: [
            { text: 'Compress a PDF', link: 'https://hyper.bentopdf.com' },
            { text: 'Getting Started', link: '/getting-started' },
            { text: 'Options', link: '/options' },
            { text: 'Benchmarks', link: '/benchmarks' },
            { text: 'BentoPDF', link: 'https://www.bentopdf.com' }
        ],

        sidebar: [
            {
                text: 'Guide',
                items: [
                    { text: 'What is Hyper Compress', link: '/' },
                    { text: 'Getting Started', link: '/getting-started' },
                    { text: 'Guarantees', link: '/guarantees' },
                    { text: 'Benchmarks', link: '/benchmarks' }
                ]
            },
            {
                text: 'Reference',
                items: [
                    { text: 'CLI', link: '/cli' },
                    { text: 'Node SDK', link: '/node-sdk' },
                    { text: 'WebAssembly', link: '/wasm' },
                    { text: 'HTTP API', link: '/http-api' },
                    { text: 'C API', link: '/c-api' },
                    { text: 'Options reference', link: '/options' }
                ]
            },
            {
                text: 'Operate',
                items: [
                    { text: 'Self-Hosting', link: '/self-hosting' },
                    { text: 'Building from Source', link: '/building' },
                    { text: 'Licensing', link: '/licensing' }
                ]
            }
        ],

        socialLinks: [
            { icon: 'github', link: 'https://github.com/alam00000/bentopdf-hyper-compress' },
            { icon: 'npm', link: 'https://www.npmjs.com/package/hyper-compress' }
        ],

        footer: {
            message: 'Dual-licensed under AGPL-3.0 and Commercial License.',
            copyright: 'Copyright © 2026 BentoPDF'
        },

        search: {
            provider: 'local'
        }
    }
})
