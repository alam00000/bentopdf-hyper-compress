import js from '@eslint/js';
import tseslint from 'typescript-eslint';

export default tseslint.config(
  { ignores: ['dist/', 'node_modules/', 'wasm/out/', 'core/', 'web/index.html'] },
  js.configs.recommended,
  ...tseslint.configs.recommended,
  {
    files: ['web/**/*.js'],
    languageOptions: {
      globals: {
        document: 'readonly',
        window: 'readonly',
        self: 'readonly',
        performance: 'readonly',
        HTMLInputElement: 'readonly',
        Worker: 'readonly',
        Blob: 'readonly',
        URL: 'readonly',
        TextDecoder: 'readonly',
        TextEncoder: 'readonly',
      },
    },
  },
  {
    files: ['tests/regression/**/*.mjs', 'bench/**/*.mjs'],
    languageOptions: {
      globals: {
        process: 'readonly',
        console: 'readonly',
        Buffer: 'readonly',
        URL: 'readonly',
        performance: 'readonly',
        setTimeout: 'readonly',
        clearTimeout: 'readonly',
      },
    },
  },
  {
    rules: {
      '@typescript-eslint/no-unused-vars': ['error', { argsIgnorePattern: '^_' }],
      'no-console': 'off',
      'no-empty': ['error', { allowEmptyCatch: true }],
      '@typescript-eslint/consistent-type-imports': 'error',
    },
  },
);
