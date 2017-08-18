/**
 * @file featex.cpp Feature extraction for pronunciation intelligibility evaluation
 */

#include "featex.h"

typedef struct alignment {
	int start, dur, cipid, score;
} alignment;

template<typename Out>
void split(const std::string &s, char delim, Out result) {
    std::stringstream ss;
    ss.str(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
    	if (!item.empty())
        	*(result++) = item;
    }
}

Feats featex(ps_decoder_t *ps, const std::vector<int16_t>& buffer, const std::string& sentence) {

	Feats feats;

	dict_t *dict;
    dict2pid_t *d2p;
    acmod_t *acmod;
    bin_mdef_t *mdef;
    ps_alignment_t *al;
    ps_alignment_iter_t *itor, *itor2;
    ps_search_t *search;
    ps_alignment_entry_t *ae;
    ps_nbest_t *nb;
    int32 score;
    hash_table_t *hyptbl;
    double frated;
    int i, j, k, n, nwords, found, ii, jj, wend, maxdur, triphone_start;

    char grammar[1000], target[10];
    char *p, *q, *r; // string manipulation pointers for constructing grammar

    size_t bufsize = 2048;

    int16 buf[bufsize];
    int16 *triphonebuf;
    size_t nread;
    int16 const *bptr;
    int nfr;

    alignment *algn;

    frated = (double) FRATE;

    dict = ps->dict;
    d2p = ps->d2p;
    acmod = ps->acmod;
    mdef = acmod->mdef;

    al = ps_alignment_init(d2p);
    ps_alignment_add_word(al, dict_wordid(dict, "<s>"), 0);

    std::vector<std::string> words;
    split(sentence, ' ', std::back_inserter(words));

    i = 0;
    nwords = words.size();
    while (i < nwords) {
    	n = dict_wordid(dict, words[i].c_str());
        if (n < 0) {
            printf("%s: unrecogized word: %s\n", "featex.cpp", words[i].c_str());
            //return 3;
        }
        ps_alignment_add_word(al, n, 0);
        i++;
    }
    ps_alignment_add_word(al, dict_wordid(dict, "</s>"), 0);
    ps_alignment_populate(al);

    search = state_align_search_init("state_align", ps->config, acmod, al);

    acmod_start_utt(acmod);
    ps_search_start(search);

    size_t arrsize = buffer.size();

  	printf("Buffer size: %u\n", arrsize);

    size_t start = 0, end = bufsize;

    while (start < end && end <= arrsize) {
	  	printf("start : %u, end : %u\n", start, end);

    	memset(buf, 0, sizeof(int16) * bufsize);
    	
    	for (ii = start, jj = 0; ii < end; ii++, jj++) {
    		buf[jj] = buffer[ii];
    	}
    	nread = end - start;
    	bptr = buf; 
        while ((nfr = acmod_process_raw(acmod, &bptr, &nread, FALSE)) > 0) {
            while (acmod->n_feat_frame > 0) {
                ps_search_step(search, acmod->output_frame);
                acmod_advance(acmod);
            }
        	printf("processed %d frames\n", nfr);
        }

        start += bufsize;
        end += bufsize;
        if (end > arrsize) {
        	end = arrsize;
        }
    }

    acmod_end_utt(acmod);
    ps_search_finish(search);

    printf("%s: aligned %d words, %d phones, and %d states\n",
        "featex.cpp", ps_alignment_n_words(al), ps_alignment_n_phones(al),
        ps_alignment_n_states(al));


    algn = (alignment *) malloc(sizeof(alignment) * ps_alignment_n_phones(al));
    n = 0;

    maxdur = 0;

    itor = ps_alignment_words(al);
    while (itor) {
        ae = ps_alignment_iter_get(itor);
        printf("%s: word '%s': %.2fs for %.2fs, score %d\n", "featex.cpp",
            dict->word[ae->id.wid].word, ae->start / frated,
            ae->duration / frated, ae->score);

        itor2 = ps_alignment_iter_down(itor);
        wend = ae->duration + ae->start;
        while (itor2) {
            ae = ps_alignment_iter_get(itor2);
            if (ae->start >= wend) break;
            printf("%s: sub-phone '%s': %.2fs for %.2fs, score %d\n",
                "featex.cpp", mdef->ciname[ae->id.pid.cipid], ae->start / frated,
                ae->duration / frated, ae->score);
            algn[n].start = ae->start;
            algn[n].dur = ae->duration;
            algn[n].score = ae->score;
            algn[n++].cipid = ae->id.pid.cipid;
            if (ae->duration > maxdur)
                maxdur = ae->duration;
            itor2 = ps_alignment_iter_next(itor2);
        }
        itor = ps_alignment_iter_next(itor);
    }

    ps_search_free(search);
    ps_alignment_free(al);

    for (i = 0; i < n; i++) {

        printf("%s: phoneme %d: %s %.2fs for %.2fs, score %d\n",
            "featex.cpp", i + 1, mdef->ciname[algn[i].cipid],
            algn[i].start / frated, algn[i].dur / frated, algn[i].score);
    }

    hyptbl = hash_table_new(175, HASH_CASE_YES); // for hypothesis deduplication


    for (i = 1; i < n; i++) {

        if (i == n-1) goto lastdiphone;

        if (i > 1) printf(" ");

        printf("%.2f %.3f", algn[i].dur / frated, 1 / log(2 - algn[i].score));
        feats.push_back(algn[i].dur / frated);
		feats.push_back(1 / log(2 - algn[i].score));

		// Populate triphone
		nread = (algn[i-1].dur + algn[i].dur + algn[i+1].dur) * FPS;
		triphonebuf = (int16 *) malloc(sizeof(int16) * (nread + 16000));
		memset(triphonebuf, 0, (nread + 16000));
		triphone_start = algn[i-1].start * FPS;
		for (ii = 8000, jj = triphone_start; jj < triphone_start + nread; ii++, jj++) {
			triphonebuf[ii] = buffer[jj];
		}

        printf("%s: triphone %d: %s-%s-%s\n", "featex.cpp", i,
            mdef->ciname[algn[i-1].cipid],
            mdef->ciname[algn[i].cipid],
            mdef->ciname[algn[i+1].cipid]);

        grammar[0] = '\0';
        strcat(grammar, "#JSGF V1.0;\ngrammar subalts;\npublic <alts> = sil1 ");
        if (algn[i-1].cipid != mdef->sil) {
             p = mdef->ciname[algn[i-1].cipid];
             q = grammar;
             while (*++q);
             while (*p) *q++ = tolower(*p++);
             *q++ = '2';
             *q = '\0';
        }
        strcat(grammar, " [ aa3 | ae3 | ah3 | ao3 | aw3 | ay3 | b3 | ch3 | d3"
               " | dh3 | eh3 | er3 | ey3 | f3 | g3 | hh3 | ih3 | iy3 | jh3"
               " | k3 | l3 | m3 | n3 | ng3 | ow3 | oy3 | p3 | r3 | s3 | sh3"
               " | sil3 | t3 | th3 | uh3 | uw3 | v3 | w3 | y3 | z3 | zh3 ] ");
        if (algn[i+1].cipid != mdef->sil) {
             p = mdef->ciname[algn[i+1].cipid];
             q = grammar;
             while (*++q);
             while (*p) *q++ = tolower(*p++);
             *q++ = '4';
             *q = '\0';
        }
        strcat(grammar, " sil5 ;\n");

        printf("%s: %s", "featex.cpp", grammar);

        ps_set_jsgf_string(ps, "subalts", grammar);
        ps_set_search(ps, "subalts");
        ps_start_utt(ps);
        ps_process_raw(ps, (const int16 *) triphonebuf, nread + 16000,
                       FALSE, TRUE);
        ps_end_utt(ps);

        nb = ps_nbest(ps);
        j = found = 0;
        target[0] = ' '; target[1] = '\0';
        strcat(target, mdef->ciname[algn[i].cipid]);
        strcat(target, "3");
        while (nb) {
            p = (char *) ps_nbest_hyp(nb, &score);
            if (p) { // some hypotheses are literally NULL
                q = p;
                while (*++q);
                if (*(q-1) == '5') { // ignore hypotheses w/o whole match

                    // ignore repeated hypotheses
                    if (hash_table_lookup(hyptbl, p, NULL) == -1) {
                        j++;
                        printf("%s: triphone hypothesis %d: %s, %d\n",
                            "featex.cpp", j, p, score);
                        hash_table_enter_int32(hyptbl, p, score);

                        if (strcasestr(p, target)) {
                            found++;
                            ps_nbest_free(nb);
                            break;
                        }
                    }
                }
            }
            nb = ps_nbest_next(nb);
        }
        if (!found) k = 42; // zero for bad recognition results or no match
        printf("%s: SUBSTITUTION: %.3f\n", "featex.cpp", (42.0 - j) / 42.0);
        printf(" %.3f", (42.0 - j) / 42.0);
        feats.push_back((42.0 - j) / 42.0);

        hash_table_empty(hyptbl);

        lastdiphone: // goto target for the final set of two phonemes

        // Populate diphone
		nread = (algn[i-1].dur + algn[i].dur) * FPS;
		triphonebuf = (int16 *) malloc(sizeof(int16) * (nread + 16000));
		memset(triphonebuf, 0, (nread + 16000));
		triphone_start = algn[i-1].start * FPS;
		for (ii = 8000, jj = triphone_start; jj < triphone_start + nread; ii++, jj++) {
			triphonebuf[ii] = buffer[jj];
		}

        printf("%s: diphone %d: %s-%s\n", "featex.cpp", i,
                mdef->ciname[algn[i-1].cipid],
                mdef->ciname[algn[i].cipid]);

        grammar[0] = '\0';
        strcat(grammar,
            "#JSGF V1.0;\ngrammar insdels;\npublic <alts> = sil1 [ ");
        p = mdef->ciname[algn[i-1].cipid];
        q = grammar;
        while (*++q);
        while (*p) *q++ = tolower(*p++);
        *q++ = '2';
        *q = '\0';
        r = q;
        strcat(grammar, " ] [  aa3| ae3 | ah3 | ao3 | aw3 | ay3 | b3  | ch3"
               " | d3  | dh3 | eh3 | er3 | ey3 | f3  | g3  | hh3 | ih3 | iy3"
               " | jh3 | k3  | l3  | m3  | n3  | ng3 | ow3 | oy3 | p3  | r3 "
               " | s3  | sh3 | sil3 | t3  | th3 | uh3 | uw3 | v3  | w3  | y3 "
               " | z3  | zh3 ] ");
        p = mdef->ciname[algn[i-1].cipid]; // first in diphone
        while (*++q) { // blank out expected phoneme from possible insertions
            if (isalpha(*q)) {
                if ((*q == tolower(*p))
                        && (((*(q+1) == '3') && *(p+1) == '\0')
                           || *(q+1) == tolower(*(p+1)))) {
                    *(q-2) = ' '; // blank out preceding '|'
                    *q = ' '; *(q+1) = ' '; *(q+2) = ' '; *(q+3) = ' ';
                } else {
                    q += 3; // advance past the rest of the phoneme
                }
            }
        }
        p = mdef->ciname[algn[i].cipid]; // second in diphone
        q = r;
        while (*++q) { // blank out expected phoneme from possible insertions
            if (isalpha(*q)) {
                if ((*q == tolower(*p))
                        && (((*(q+1) == '3') && *(p+1) == '\0')
                           || *(q+1) == tolower(*(p+1)))) {
                    *(q-2) = ' '; // blank out preceding '|'
                    *q = ' '; *(q+1) = ' '; *(q+2) = ' '; *(q+3) = ' ';
                } else {
                    q += 3; // advance past the rest of the phoneme
                }
            }
        }
        if (algn[i].cipid != mdef->sil) {
             p = mdef->ciname[algn[i].cipid];
             q = grammar;
             while (*++q);
             while (*p) *q++ = tolower(*p++);
             *q++ = '4';
             *q = '\0';
        }
        strcat(grammar, " sil5 ;\n");

        printf("%s: %s", "featex.cpp", grammar);

        ps_set_jsgf_string(ps, "insdels", grammar);
        ps_set_search(ps, "insdels");
        ps_start_utt(ps);
        ps_process_raw(ps, (const int16 *) triphonebuf, (nread + 16000), FALSE, TRUE);
        ps_end_utt(ps);

        nb = ps_nbest(ps);
        j = k = found = 0;
        while (nb) {
            p = (char *) ps_nbest_hyp(nb, &score);
            if (p) { // some hypotheses are literally NULL
                q = p;
                while (*++q);
                if (*(q-1) == '5') { // ignore hypotheses w/o whole match

                    // ignore repeated hypotheses
                    if (hash_table_lookup(hyptbl, p, NULL) == -1) {
                        j++;
                        printf("%s: diphone hypothesis %d: %s, %d\n",
                            "featex.cpp", j, p, score);
                        hash_table_enter_int32(hyptbl, p, score);

                        if (!strstr(p, "2 ")) k++;
                        if (strstr(p, "3 ")) k++;

                        if (strstr(p, "2 ") && !strstr(p, "3 ")) {
                            found++;
                            ps_nbest_free(nb);
                            break;
                        }
                    }
                }
            }
            nb = ps_nbest_next(nb);
        }
        if (j == 0)
            k = 160; // zero for bad recognition results
        else if (!found) {
            k += 80; // add half the range if the preferred hypothesis missed
            if (k > 160) k = 160; // clamp
        }
        printf("%s: INS/DEL: %.3f\n", "featex.cpp", (160.0 - k) / 160);
        printf(" %.3f", (160.0 - k) / 160);
        feats.push_back((160.0 - k) / 160);

        hash_table_empty(hyptbl);
    }

    free(triphonebuf);
    free(algn);

    return feats;
}