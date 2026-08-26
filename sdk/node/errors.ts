export type HyperErrorCode =
  | 'decrypt_failed'
  | 'engine_error'
  | 'timeout'
  | 'cancelled';

export class HyperError extends Error {
  readonly code: HyperErrorCode;
  constructor(code: HyperErrorCode, message?: string) {
    super(message ?? code);
    this.name = 'HyperError';
    this.code = code;
  }
}
