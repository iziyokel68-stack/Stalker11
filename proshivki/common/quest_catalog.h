/**
 * S.T.A.L.K.E.R. — каталог доски заданий на EEPROM CH0
 *
 *   0x0100  каталог (заголовок 8 Б + до 24 карточек по 48 Б)
 *   0x00A4  заявка ПДА «взять / сдать» (24 Б), пока TXN свободен
 *
 * Режимы выдачи (мастер задаёт на доске):
 *   timeout — эксклюзивно; если не сдано за timeout_min (по умолчанию 120),
 *             снова висит для других
 *   oneshot — эксклюзивно; после взятия больше не появляется (даже по таймауту)
 *   shared  — висит для всех, брать могут несколько ПДА
 */
#ifndef STALKER_QUEST_CATALOG_H
#define STALKER_QUEST_CATALOG_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

#define QUEST_CAT_BASE          0x0100  /* listed overlay for PDA */
#define QUEST_CAT_FULL_BASE     0x0800  /* full catalog backup on cassette */
#define QUEST_CAT_HDR_SIZE      8
#define QUEST_CAT_REC_SIZE      48
#define QUEST_CAT_MAX           24
#define QUEST_CAT_MAGIC0        'Q'
#define QUEST_CAT_MAGIC1        'S'
#define QUEST_CAT_VERSION       1

#define QUEST_TAKE_BASE         0x00A4
#define QUEST_TAKE_SIZE         24
#define QUEST_TAKE_MAGIC0       'T'
#define QUEST_TAKE_MAGIC1       'Q'

#define QUEST_FLAG_HIDDEN       0x01
#define QUEST_MODE_SHIFT        1
#define QUEST_MODE_MASK         0x06
#define QUEST_MODE_TIMEOUT      0
#define QUEST_MODE_ONESHOT      1
#define QUEST_MODE_SHARED       2
#define QUEST_TIMEOUT_DEFAULT   120

#define QUEST_TAKE_COMPLETE     0x01

#define QUEST_CLAIM_FREE        0
#define QUEST_CLAIM_HELD        1
#define QUEST_CLAIM_DONE        2
#define QUEST_CLAIM_GONE        3

typedef struct {
  char id[9];
  char title[25];
  int32_t rub;
  uint8_t hidden;
  uint8_t mode;
  uint16_t timeout_min;
} QuestCatRec;

static inline uint8_t quest_pack_flags(uint8_t hidden, uint8_t mode) {
  uint8_t f = hidden ? QUEST_FLAG_HIDDEN : 0;
  f |= (uint8_t)((mode & 3) << QUEST_MODE_SHIFT);
  return f;
}

static inline uint8_t quest_flag_hidden(uint8_t flags) {
  return (flags & QUEST_FLAG_HIDDEN) ? 1 : 0;
}

static inline uint8_t quest_flag_mode(uint8_t flags) {
  return (uint8_t)((flags & QUEST_MODE_MASK) >> QUEST_MODE_SHIFT);
}

static inline uint16_t quest_u16(const uint8_t *p) {
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static inline void quest_put_u16(uint8_t *p, uint16_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static inline int32_t quest_i32(const uint8_t *p) {
  return (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) |
                   ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24));
}

static inline void quest_put_i32(uint8_t *p, int32_t v) {
  p[0] = (uint8_t)(v & 0xFF);
  p[1] = (uint8_t)((v >> 8) & 0xFF);
  p[2] = (uint8_t)((v >> 16) & 0xFF);
  p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static inline uint16_t quest_crc16(const uint8_t *data, size_t len) {
  uint16_t crc = 0xFFFF;
  for (size_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (int b = 0; b < 8; b++)
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
  }
  return crc;
}

static inline void quest_cat_pack_hdr(uint8_t *hdr, uint8_t count, uint16_t crc) {
  memset(hdr, 0, QUEST_CAT_HDR_SIZE);
  hdr[0] = QUEST_CAT_MAGIC0;
  hdr[1] = QUEST_CAT_MAGIC1;
  hdr[2] = QUEST_CAT_VERSION;
  hdr[3] = count;
  quest_put_u16(hdr + 4, crc);
}

static inline int quest_cat_parse_hdr(const uint8_t *hdr, uint8_t *count, uint16_t *crc) {
  if (hdr[0] != QUEST_CAT_MAGIC0 || hdr[1] != QUEST_CAT_MAGIC1)
    return 0;
  if (hdr[2] != QUEST_CAT_VERSION)
    return 0;
  if (count)
    *count = hdr[3];
  if (crc)
    *crc = quest_u16(hdr + 4);
  return 1;
}

static inline void quest_cat_pack_rec(uint8_t *dst, const QuestCatRec *q) {
  memset(dst, 0, QUEST_CAT_REC_SIZE);
  memcpy(dst, q->id, 8);
  memcpy(dst + 8, q->title, 24);
  quest_put_i32(dst + 32, q->rub);
  dst[36] = quest_pack_flags(q->hidden, q->mode);
  uint16_t tmin = q->timeout_min ? q->timeout_min : QUEST_TIMEOUT_DEFAULT;
  quest_put_u16(dst + 37, tmin);
}

static inline void quest_cat_parse_rec(const uint8_t *src, QuestCatRec *q) {
  memset(q, 0, sizeof(*q));
  memcpy(q->id, src, 8);
  q->id[8] = '\0';
  memcpy(q->title, src + 8, 24);
  q->title[24] = '\0';
  q->rub = quest_i32(src + 32);
  uint8_t flags = src[36];
  q->hidden = quest_flag_hidden(flags);
  q->mode = quest_flag_mode(flags);
  q->timeout_min = quest_u16(src + 37);
  if (!q->timeout_min)
    q->timeout_min = QUEST_TIMEOUT_DEFAULT;
}

/** 1 = висит на доске и его можно взять. */
static inline int quest_is_listed(uint8_t mode, uint8_t claim, uint16_t elapsed_min,
                                  uint16_t timeout_min) {
  if (mode == QUEST_MODE_SHARED)
    return 1;
  if (claim == QUEST_CLAIM_DONE || claim == QUEST_CLAIM_GONE)
    return 0;
  if (claim == QUEST_CLAIM_FREE)
    return 1;
  if (claim == QUEST_CLAIM_HELD) {
    if (mode == QUEST_MODE_ONESHOT)
      return 0;
    return elapsed_min >= (timeout_min ? timeout_min : QUEST_TIMEOUT_DEFAULT);
  }
  return 1;
}

static inline uint8_t quest_next_claim(uint8_t mode, uint8_t claim,
                                       uint16_t elapsed_min, uint16_t timeout_min,
                                       int completed) {
  if (completed) {
    if (mode == QUEST_MODE_SHARED)
      return QUEST_CLAIM_FREE;
    return QUEST_CLAIM_DONE;
  }
  if (claim != QUEST_CLAIM_HELD)
    return claim;
  if (elapsed_min < (timeout_min ? timeout_min : QUEST_TIMEOUT_DEFAULT))
    return QUEST_CLAIM_HELD;
  if (mode == QUEST_MODE_ONESHOT)
    return QUEST_CLAIM_GONE;
  return QUEST_CLAIM_FREE;
}

static inline uint8_t quest_mode_from_name(const char *s) {
  if (!s || !s[0])
    return QUEST_MODE_TIMEOUT;
  if (s[0] == 'o' || s[0] == 'O' || s[0] == '1')
    return QUEST_MODE_ONESHOT;
  if (s[0] == 's' || s[0] == 'S' || s[0] == '2')
    return QUEST_MODE_SHARED;
  return QUEST_MODE_TIMEOUT;
}

static inline void quest_take_pack(uint8_t *dst, const char *qid, const char *uid,
                                   uint8_t flags) {
  memset(dst, 0, QUEST_TAKE_SIZE);
  dst[0] = QUEST_TAKE_MAGIC0;
  dst[1] = QUEST_TAKE_MAGIC1;
  if (qid) {
    size_t n = strlen(qid);
    if (n > 8)
      n = 8;
    memcpy(dst + 2, qid, n);
  }
  if (uid) {
    size_t n = strlen(uid);
    if (n > 12)
      n = 12;
    memcpy(dst + 10, uid, n);
  }
  dst[22] = flags;
}

static inline int quest_take_parse(const uint8_t *src, char *qid, char *uid,
                                   uint8_t *flags) {
  if (src[0] != QUEST_TAKE_MAGIC0 || src[1] != QUEST_TAKE_MAGIC1)
    return 0;
  if (qid) {
    memcpy(qid, src + 2, 8);
    qid[8] = '\0';
  }
  if (uid) {
    memcpy(uid, src + 10, 12);
    uid[12] = '\0';
  }
  if (flags)
    *flags = src[22];
  return 1;
}

#endif
