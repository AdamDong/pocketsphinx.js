/**
 * @file featex.h Feature extraction for pronunciation intelligibility evaluation
 */

#ifndef __FEATEX_H__
#define __FEATEX_H__

#include <string>
#include <vector>
#include <sstream>
#include <iterator>
#include <stdint.h>

#include "pocketsphinx.h"
#include "ps_alignment.h"
#include "state_align_search.h"
#include "pocketsphinx_internal.h"
#include "ps_search.h"

#include <ctype.h>
#include <math.h>

#define FRATE 65
#define SAMPRATE 16000
#define FPS (SAMPRATE / FRATE)

//#define _GNU_SOURCE // strcasestr() non-standard string search

typedef std::vector<float> Feats;

Feats featex(ps_decoder_t *ps, const std::vector<int16_t>& buffer, const std::string& sentence);

#endif /* __FEATEX_H__ */