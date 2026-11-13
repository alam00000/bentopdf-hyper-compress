import { defineConfig } from 'vitepress'

const SITE_URL = 'https://hyper.bentopdf.com'

export default defineConfig({
    title: "Hyper Compress Docs",
    description: "Documentation for Hyper Compress - the high fidelity, content preserving PDF compression engine built by the BentoPDF team",
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
            { text: 'Quickstart', link: '/getting-started' },
            { text: 'Guides', link: '/guides/choosing-settings' },
            { text: 'API reference', link: '/node-sdk' },
            { text: 'Benchmarks', link: '/benchmarks' },
            { text: 'BentoPDF', link: 'https://www.bentopdf.com' }
        ],

        sidebar: [
            {
                text: 'Get started',
                items: [
                    { text: 'Overview', link: '/' },
                    { text: 'Quickstart', link: '/getting-started' },
                    { text: 'Platforms and surfaces', link: '/platforms' }
                ]
            },
            {
                text: 'Guides',
                items: [
                    { text: 'Choose the right settings', link: '/guides/choosing-settings' },
                    { text: 'Hit a file size target', link: '/guides/size-targets' },
                    { text: 'Encrypted PDFs', link: '/guides/encrypted-pdfs' },
                    { text: 'Signed documents', link: '/guides/signed-documents' },
                    { text: 'PDF/A archives', link: '/guides/pdfa' },
                    { text: 'Scanned documents', link: '/guides/scanned-documents' },
                    { text: 'Colour and print', link: '/guides/colour-and-print' },
                    { text: 'Document structure and cleanup', link: '/guides/document-structure' },
                    { text: 'Errors, timeouts and cancellation', link: '/guides/errors-and-cancellation' },
                    { text: 'Running in production', link: '/guides/production' }
                ]
            },
            {
                text: 'Reference',
                items: [
                    { text: 'CLI', link: '/cli' },
                    { text: 'Node.js SDK', link: '/node-sdk' },
                    { text: 'WebAssembly', link: '/wasm' },
                    { text: 'HTTP API', link: '/http-api' },
                    { text: 'Rust crate', link: '/rust' },
                    { text: 'C API', link: '/c-api' },
                    { text: 'Options', link: '/options' },
                    { text: 'Compression report', link: '/report' },
                    { text: 'Errors', link: '/errors' },
                    { text: 'Warnings', link: '/warnings' }
                ]
            },
            {
                text: 'Operate',
                items: [
                    { text: 'Self-hosting', link: '/self-hosting' },
                    { text: 'Building from source', link: '/building' }
                ]
            },
            {
                text: 'Resources',
                items: [
                    { text: 'Guarantees', link: '/guarantees' },
                    { text: 'Benchmarks', link: '/benchmarks' },
                    { text: 'Licensing', link: '/licensing' },
                    { text: 'FAQ and troubleshooting', link: '/faq' }
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
        },

        outline: { level: [2, 3], label: 'On this page' },

        editLink: {
            pattern: 'https://github.com/alam00000/bentopdf-hyper-compress/edit/main/docs/:path',
            text: 'Edit this page on GitHub'
        }
    }
})
