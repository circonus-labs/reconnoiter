#ifndef OETS_ASN1_HELPER_H
#define OETS_ASN1_HELPER_H

#include <time.h>
#include <openssl/x509.h>
time_t OETS_ASN1_TIME_get(ASN1_TIME *a, int *err);

#endif
