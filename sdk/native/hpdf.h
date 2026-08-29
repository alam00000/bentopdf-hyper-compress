#ifndef HPDF_H
#define HPDF_H

#ifdef __cplusplus
extern "C" {
#endif

#define HPDF_VERSION "1.0.0"

int hpdf_compress_file(const char* in_path,
                       const char* out_path,
                       const int* option_ids,
                       const int* option_values,
                       int option_count,
                       const char* password);

unsigned char* hpdf_compress_buffer(const unsigned char* in_data,
                                    unsigned long in_size,
                                    const int* option_ids,
                                    const int* option_values,
                                    int option_count,
                                    const char* password,
                                    unsigned long* out_size,
                                    int* out_status);

void hpdf_free(unsigned char* buffer);

const char* hpdf_last_error(void);

const char* hpdf_version(void);

#ifdef __cplusplus
}
#endif

#endif
