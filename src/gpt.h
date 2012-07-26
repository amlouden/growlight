#ifndef GROWLIGHT_GPT
#define GROWLIGHT_GPT

#ifdef __cplusplus
extern "C" {
#endif

#include <wchar.h>
#include <stdint.h>

struct device;

int new_gpt(struct device *);
int zap_gpt(struct device *);
int add_gpt(struct device *,const wchar_t *,uintmax_t);

#ifdef __cplusplus
}
#endif

#endif