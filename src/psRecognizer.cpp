#include "psRecognizer.h"
#include "pocketsphinx.h"
#include "pocketsphinxjs-config.h"

namespace pocketsphinxjs {
  typedef std::map<std::string, std::string> StringsMapType;
  typedef std::map<std::string, std::string>::iterator StringsMapIterator;

  // Implemented later in this file
  ReturnType parseStringList(const std::string &, StringsSetType*, std::string*);

  Recognizer::Recognizer(): is_fsg(true), is_recording(false), current_hyp(""), grammar_index(0) {
    Config c;
    if (init(c) != SUCCESS) cleanup();
  }

  Recognizer::Recognizer(const Config& config) : is_fsg(true), is_recording(false), current_hyp(""), grammar_index(0) {
    if (init(config) != SUCCESS) cleanup();
  }

  ReturnType Recognizer::reInit(const Config& config) {
    ReturnType r = init(config);
    if (r != SUCCESS) cleanup();
    return r;
  }

  ReturnType Recognizer::addWords(const std::vector<Word>& words) {
    if (decoder == NULL) return BAD_STATE;
    for (int i=0 ; i<words.size() ; ++i) {
      // This case is not properly handeled by ps_add_word, so we treat it separately
      if (words.at(i).pronunciation.size() == 0) return RUNTIME_ERROR;
      if (ps_add_word(decoder, words.at(i).word.c_str(), words.at(i).pronunciation.c_str(), 1) < 0) return RUNTIME_ERROR;
    }
    return SUCCESS;
  }

  ReturnType Recognizer::addGrammar(Integers& id, const Grammar& grammar) {
    if (decoder == NULL) return BAD_STATE;
    std::ostringstream grammar_name;
    grammar_name << grammar_index;
    grammar_names.push_back(grammar_name.str());
    current_grammar = fsg_model_init(grammar_names.back().c_str(), logmath, 1.0, grammar.numStates);
    if (current_grammar == NULL)
      return RUNTIME_ERROR;
    current_grammar->start_state = grammar.start;
    current_grammar->final_state = grammar.end;
    for (int i=0;i<grammar.transitions.size();i++) {
      if ((grammar.transitions.at(i).word.size() > 0) && (ps_lookup_word(decoder, grammar.transitions.at(i).word.c_str())))
	fsg_model_trans_add(current_grammar, grammar.transitions.at(i).from, grammar.transitions.at(i).to, grammar.transitions.at(i).logp, fsg_model_word_add(current_grammar, grammar.transitions.at(i).word.c_str()));
      else
	fsg_model_null_trans_add(current_grammar, grammar.transitions.at(i).from, grammar.transitions.at(i).to, grammar.transitions.at(i).logp);
    }
    fsg_model_add_silence(current_grammar, "<sil>", -1, 1.0);

    if(ps_set_fsg(decoder, grammar_names.back().c_str(), current_grammar)) {
      return RUNTIME_ERROR;
    }
    if (id.size() == 0) id.push_back(grammar_index);
    else id.at(0) = grammar_index;
    grammar_index++;
    // We switch to the newly added grammar right away
    if (ps_set_search(decoder, grammar_names.back().c_str())) {
      return RUNTIME_ERROR;
    }
    return SUCCESS;
  }


  ReturnType Recognizer::addKeyword(Integers& id, const std::string& keyphrase) {
    if (decoder == NULL) return BAD_STATE;
    std::ostringstream search_name;
    search_name << grammar_index;
    grammar_names.push_back(search_name.str());
    if(ps_set_keyphrase(decoder, grammar_names.back().c_str(), keyphrase.c_str())) {
      return RUNTIME_ERROR;
    }
    if (id.size() == 0) id.push_back(grammar_index);
    else id.at(0) = grammar_index;
    grammar_index++;
    // We switch to the newly added search right away
    if (ps_set_search(decoder, grammar_names.back().c_str())) {
      return RUNTIME_ERROR;
    }
    return SUCCESS;
  }


  ReturnType Recognizer::switchGrammar(int id) {
    return switchSearch(id);
  }

  ReturnType Recognizer::switchSearch(int id) {
    if (decoder == NULL) return BAD_STATE;
    if ((id < 0) || (id >= grammar_names.size())) return BAD_ARGUMENT;
    if(ps_set_search(decoder, grammar_names.at(id).c_str())) {
      return RUNTIME_ERROR;
    }
    return SUCCESS;
  }

  ReturnType Recognizer::start() {
    if ((decoder == NULL) || (is_recording)) return BAD_STATE;
    if ((ps_start_utt(decoder) < 0) || (ps_start_stream(decoder) < 0)) {
      return RUNTIME_ERROR;
    }
    current_hyp = "";
    is_recording = true;
    return SUCCESS;
  }

  ReturnType Recognizer::stop() {
    if ((decoder == NULL) || (!is_recording)) return BAD_STATE;
    if (ps_end_utt(decoder) < 0) {
      return RUNTIME_ERROR;
    }
    const char* h = ps_get_hyp(decoder, NULL);
    current_hyp = (h == NULL) ? "" : h;
    is_recording = false;
    return SUCCESS;
  }

  ReturnType Recognizer::process(const std::vector<int16_t>& buffer) {
    if ((decoder == NULL) || (!is_recording)) return BAD_STATE;
    if (buffer.size() == 0)
      return RUNTIME_ERROR;
    ps_process_raw(decoder, (short int *) &buffer[0], buffer.size(), 0, 0);
    const char* h = ps_get_hyp(decoder, NULL);
    current_hyp = (h == NULL) ? "" : h;
    return SUCCESS;
  }

  ReturnType Recognizer::wordAlign(const std::vector<int16_t>& buffer, const std::string& word) {
    if ((decoder == NULL) || (!is_recording)) return BAD_STATE;
    if (buffer.size() == 0)
      return RUNTIME_ERROR;
    //ps_process_raw(decoder, (short int *) &buffer[0], buffer.size(), 0, 0);
    //const char* h = ps_get_hyp(decoder, NULL);
    //current_hyp = (h == NULL) ? "" : h;

    //int16 buf[2048];
    //size_t nread;
    //int16 const *bptr;
    int nfr;

    dict = decoder->dict;
    d2p = decoder->d2p;
    acmod = decoder->acmod;

    al = ps_alignment_init(d2p);
    ps_alignment_add_word(al, dict_wordid(dict, "<s>"), 0);
    ps_alignment_add_word(al, dict_wordid(dict, word.c_str()), 0);
    ps_alignment_add_word(al, dict_wordid(dict, "</s>"), 0);
    ps_alignment_populate(al);

    search = state_align_search_init("state_align", cmd_line, acmod, al);

    acmod_start_utt(acmod);
    ps_search_start(search);

    const int16 * bufarr = (int16 *) &buffer[0];
    size_t bufsize = buffer.size();
    /*
    short int * bufarr = (short int *) &buffer[0];
    size_t bufsize = buffer.size();

    size_t cnt = 0;

    
    while(cnt < bufsize) {
    	memset(buf, 0, sizeof(int16) * 2048);
    	int i, j;
    	for (int i = cnt, j = 0; i < bufsize && j < 2048; i++, j++) {
    		buf[j] = bufarr[i];
    	}
    	cnt += 2048;
    	nread = j;
    	bptr = buf; 
        while ((nfr = acmod_process_raw(acmod, &bptr, &nread, FALSE)) > 0) {
            while (acmod->n_feat_frame > 0) {
                ps_search_step(search, acmod->output_frame);
                acmod_advance(acmod);
            }
        //printf("processed %d frames\n", nfr);
        }
    }
    */

    while ((nfr = acmod_process_raw(acmod, &bufarr, &bufsize, FALSE)) > 0) {
        while (acmod->n_feat_frame > 0) {
            ps_search_step(search, acmod->output_frame);
            acmod_advance(acmod);
        }
    //printf("processed %d frames\n", nfr);
    }

    acmod_end_utt(acmod);
    ps_search_finish(search);

    return SUCCESS;
  }

  ReturnType Recognizer::getWordAlignSeg(Segmentation& seg) {
    if (decoder == NULL) return BAD_STATE;
    seg.clear();

    ps_alignment_iter_t *itor = ps_alignment_phones(al);
    while (itor) {
        ae = ps_alignment_iter_get(itor);
        SegItem segItem;
      	segItem.word = ae->id.pid.cipid;
        segItem.start = ae->start / 100.0;
        segItem.end = ae->duration / 100.0;
        segItem.ascr = ae->score;
        segItem.lscr = 0;
        seg.push_back(segItem);
        itor = ps_alignment_iter_next(itor);
    }

    return SUCCESS;
  }

  std::string Recognizer::lookupWord(const std::string& word) {
    std::string output = "";
    if (word.size() > 0) {
      char * result = ps_lookup_word(decoder, word.c_str());
      if (result != NULL)
	output = result;
    }
    return output;
  }

  Recognizer::~Recognizer() {
    cleanup();
  }

  std::string Recognizer::getHyp() {
    return current_hyp;
  }

  ReturnType Recognizer::getHypseg(Segmentation& seg) {
    if (decoder == NULL) return BAD_STATE;
    seg.clear();
    int32 sfh=0, efh=0;
    int32 ascr=0, lscr=0, lback=0;
    std::string hseg;
    ps_seg_t *itor = ps_seg_iter(decoder);
    while (itor) {
      SegItem segItem;
      segItem.word = ps_seg_word(itor);
      ps_seg_frames(itor, &sfh, &efh);
      ps_seg_prob(itor, &ascr, &lscr, &lback);
      segItem.start = sfh;
      segItem.end = efh;
      segItem.ascr = ascr;
      segItem.lscr = lscr;
      seg.push_back(segItem);
      itor = ps_seg_next(itor);
    }
    return SUCCESS;
  }

  void Recognizer::cleanup() {
    if (decoder) ps_free(decoder);
    if (logmath) logmath_free(logmath);
    if (search) ps_search_free(search);
    if (al) ps_alignment_free(al);
    decoder = NULL;
    logmath = NULL;
  }

  ReturnType Recognizer::init(const Config& config) {
#ifdef HMM_FOLDERS
    parseStringList(HMM_FOLDERS, &acoustic_models, &default_acoustic_model);
#endif /* HMM_FOLDERS */
#ifdef LM_FILES
    parseStringList(LM_FILES, &language_models, &default_language_model);
#endif /* LM_FILES */
#ifdef DICT_FILES
    parseStringList(DICT_FILES, &dictionaries, &default_dictionary);
#endif /* DICT_FILES */

    const arg_t cont_args_def[] = {
      POCKETSPHINX_OPTIONS,
      { "-argfile",
	ARG_STRING,
	NULL,
	"Argument file giving extra arguments." },
      { "-adcdev",
	ARG_STRING,
	NULL,
	"Name of audio device to use for input." },
      { "-infile",
	ARG_STRING,
	NULL,
	"Audio file to transcribe." },
      { "-time",
	ARG_BOOLEAN,
	"no",
	"Print word times in file transcription." },
      CMDLN_EMPTY_OPTION
    };
    grammar_names.push_back("_default");
    grammar_index++;
    std::map<std::string, std::string> parameters;
    for (int i=0 ; i< config.size() ; ++i)
      parameters[config[i].key] = config[i].value;
    
    if (parameters.find("-hmm") == parameters.end())
      parameters["-hmm"] = default_acoustic_model;
    if (parameters.find("-bestpath") == parameters.end())
      parameters["-bestpath"] = "yes";
    if (parameters.find("-remove_noise") == parameters.end())
      parameters["-remove_noise"] = "no";
    if (parameters.find("-remove_silence") == parameters.end())
      parameters["-remove_silence"] = "no";

    int argc = 2 * parameters.size();
    char ** argv = new char*[argc];
    int index = 0;
    for (StringsMapIterator i = parameters.begin() ; i != parameters.end(); ++i) {
      if (i->first == "-lm") is_fsg = false;
      argv[index++] = (char*) i->first.c_str();
      argv[index++] = (char*) i->second.c_str();
    }

    cmd_line = cmd_ln_parse_r(NULL, cont_args_def, argc, argv, FALSE);
    if (cmd_line == NULL) {
      delete [] argv;
      return RUNTIME_ERROR;
    }
    decoder = ps_init(cmd_line);
    delete [] argv;
    if (decoder == NULL) {
      return RUNTIME_ERROR;
    }
    logmath = logmath_init(1.0001, 0, 0);
    if (logmath == NULL) {
      return RUNTIME_ERROR;
    }
    return SUCCESS;
  }

  /*******************************************
   *
   * Parses the string with available models and fills in
   * the default model and the available models
   * @param string to parse, the models are separated with ";"
   * @return 0 if successful, always successful
   *
   *****************************************/
  ReturnType parseStringList(const std::string & list, StringsSetType* strings_set, std::string* default_string = NULL) {
    if ((strings_set == NULL) || (list.size() == 0))
      return BAD_ARGUMENT;
    std::string separator = ";";
    std::string::size_type index;
    std::string first_string = list.substr(0, index = list.find(separator));
    if (default_string != NULL)
      *default_string = first_string;
    strings_set->insert(first_string);
    while((index != std::string::npos) && (index != list.size() -1))
      strings_set->insert(list.substr(index+1, -1 -index +(index = list.find(separator, index + 1))));
    return SUCCESS;
  }
} // namespace pocketsphinxjs
