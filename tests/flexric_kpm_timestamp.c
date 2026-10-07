#include "kpm_timestamp.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef FLEXRIC_ASN_BOUNDARY_TEST
#include "dec_ric_ind_hdr_frm_1.h"
#include "enc_ric_ind_hdr_frm_1.h"

static void check_asn_boundary(void);
#endif

static void require_equal(const char* name, uint64_t actual, uint64_t expected)
{
  if (actual != expected) {
    fprintf(stderr, "%s: got %" PRIu64 ", expected %" PRIu64 "\n", name, actual, expected);
    exit(EXIT_FAILURE);
  }
}

static void check_wire_vector(const char* name,
                              const uint8_t bytes[8],
                              uint64_t expected_timestamp,
                              int64_t reference_unix_seconds,
                              uint64_t expected_unix_us)
{
  const uint64_t timestamp = kpm_timestamp_read_be(bytes);
  require_equal(name, timestamp, expected_timestamp);
  require_equal(name,
                kpm_timestamp_to_unix_us(timestamp, reference_unix_seconds),
                expected_unix_us);

  uint8_t written[8];
  kpm_timestamp_write_be(written, expected_timestamp);
  if (memcmp(written, bytes, sizeof(written)) != 0) {
    fprintf(stderr, "%s: big-endian bytes differ\n", name);
    exit(EXIT_FAILURE);
  }
}

int main(void)
{
  /* Independent wire samples: these expected bytes do not use the encoder. */
  static const uint8_t ocudu_bytes[8] = {0xee, 0x70, 0xc5, 0x80, 0x3f, 0xfc, 0x4f, 0x60};
  static const uint8_t quarter_second_bytes[8] = {0xee, 0x70, 0xc5, 0x80, 0x40, 0x00, 0x00, 0x00};
  static const uint8_t unix_epoch_bytes[8] = {0x83, 0xaa, 0x7e, 0x80, 0x00, 0x00, 0x00, 0x00};
  static const uint8_t before_rollover_bytes[8] = {0xff, 0xff, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00};
  static const uint8_t rollover_bytes[8] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
  static const uint8_t after_rollover_bytes[8] = {0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00};

  check_wire_vector("OCUDU header", ocudu_bytes, UINT64_C(0xee70c5803ffc4f60),
                    INT64_C(1791379200), UINT64_C(1791379200249943));
  check_wire_vector("quarter second", quarter_second_bytes, UINT64_C(0xee70c58040000000),
                    INT64_C(1791379200), UINT64_C(1791379200250000));
  check_wire_vector("Unix epoch", unix_epoch_bytes, UINT64_C(0x83aa7e8000000000),
                    INT64_C(0), UINT64_C(0));
  check_wire_vector("before NTP rollover", before_rollover_bytes, UINT64_C(0xffffffff80000000),
                    INT64_C(2085978496), UINT64_C(2085978495500000));
  check_wire_vector("NTP rollover", rollover_bytes, UINT64_C(0),
                    INT64_C(2085978496), UINT64_C(2085978496000000));
  check_wire_vector("after NTP rollover", after_rollover_bytes, UINT64_C(0x0000000080000000),
                    INT64_C(2085978496), UINT64_C(2085978496500000));

  require_equal("encoder quarter second",
                kpm_timestamp_from_unix_us(UINT64_C(1791379200250000)),
                UINT64_C(0xee70c58040000000));
  require_equal("encoder before rollover",
                kpm_timestamp_from_unix_us(UINT64_C(2085978495500000)),
                UINT64_C(0xffffffff80000000));
  require_equal("encoder after rollover",
                kpm_timestamp_from_unix_us(UINT64_C(2085978496500000)),
                UINT64_C(0x0000000080000000));

  /* Preserve every representable microsecond, including 1 and 999999. */
  for (uint64_t fraction_us = 0; fraction_us < UINT64_C(1000000); ++fraction_us) {
    const uint64_t unix_us = UINT64_C(1791379200000000) + fraction_us;
    const uint64_t timestamp = kpm_timestamp_from_unix_us(unix_us);
    require_equal("fractional microsecond roundtrip",
                  kpm_timestamp_to_unix_us(timestamp, INT64_C(1791379200)), unix_us);
  }

  const int64_t now_us = INT64_C(1791379200251043);
  const uint64_t header_us = kpm_timestamp_to_unix_us(kpm_timestamp_read_be(ocudu_bytes),
                                                     INT64_C(1791379200));
  const int64_t report_age_us = now_us - (int64_t)header_us;
  if (report_age_us != INT64_C(1100)) {
    fprintf(stderr, "report age: got %" PRId64 ", expected 1100\n", report_age_us);
    return EXIT_FAILURE;
  }

#ifdef FLEXRIC_ASN_BOUNDARY_TEST
  check_asn_boundary();
#endif

  puts("PASS: KPM v3 wire vectors, NTP era rollover, 1000000 fractional roundtrips, report age 1100 us");
  return EXIT_SUCCESS;
}

#ifdef FLEXRIC_ASN_BOUNDARY_TEST
static void check_asn_boundary(void)
{
  uint8_t wire[8] = {0xee, 0x70, 0xc5, 0x80, 0x3f, 0xfc, 0x4f, 0x60};
  E2SM_KPM_IndicationHeader_Format1_t asn_header = {0};
  asn_header.colletStartTime.buf = wire;
  asn_header.colletStartTime.size = sizeof(wire);
  const kpm_ric_ind_hdr_format_1_t decoded = kpm_dec_ind_hdr_frm_1_asn(&asn_header);
  require_equal("ASN decoder OCUDU literal bytes", decoded.collectStartTime,
                UINT64_C(1791379200249943));

  const kpm_ric_ind_hdr_format_1_t input = {.collectStartTime = UINT64_C(1791379200250000)};
  E2SM_KPM_IndicationHeader_Format1_t* encoded = kpm_enc_ind_hdr_frm_1_asn(&input);
  static const uint8_t expected[8] = {0xee, 0x70, 0xc5, 0x80, 0x40, 0x00, 0x00, 0x00};
  if (encoded->colletStartTime.size != sizeof(expected) ||
      memcmp(encoded->colletStartTime.buf, expected, sizeof(expected)) != 0) {
    fputs("ASN encoder: expected canonical NTP big-endian bytes\n", stderr);
    exit(EXIT_FAILURE);
  }
  ASN_STRUCT_FREE(asn_DEF_E2SM_KPM_IndicationHeader_Format1, encoded);
  puts("PASS: actual ASN codec decodes independent OCUDU bytes and encodes canonical NTP bytes");
}
#endif
