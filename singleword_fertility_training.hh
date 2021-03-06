/*** written by Thomas Schoenemann. Started as a private person without employment, November 2009 ***/
/*** continued at Lund University, Sweden, January 2010 - March 2011, as a private person and ***/
/*** at the University of Düsseldorf, Germany, January - April 2012 ***/

#ifndef SINGLEWORD_FERTILITY_TRAINING_HH
#define SINGLEWORD_FERTILITY_TRAINING_HH

#include "mttypes.hh"
#include "vector.hh"
#include "tensor.hh"

#include "hmm_training.hh"

#include <map>
#include <set>

enum HillclimbingMode {HillclimbingReuse,HillclimbingRestart};

/*abstract*/ class FertilityModelTrainer {
public:

  FertilityModelTrainer(const Storage1D<Storage1D<uint> >& source_sentence,
                        const LookupTable& slookup,
                        const Storage1D<Storage1D<uint> >& target_sentence,
                        SingleWordDictionary& dict,
                        const CooccuringWordsType& wcooc,
                        uint nSourceWords, uint nTargetWords,
			const floatSingleWordDictionary& prior_weight,
			bool och_ney_empty_word, bool smoothed_l0_,
			double l0_beta,	double l0_fertpen, bool no_factorial,
                        const std::map<uint,std::set<std::pair<AlignBaseType,AlignBaseType> > >& sure_ref_alignments,
                        const std::map<uint,std::set<std::pair<AlignBaseType,AlignBaseType> > >& possible_ref_alignments,
			const Math1D::Vector<double>& log_table,
                        uint fertility_limit = 10000);


  virtual std::string model_name() const = 0;

  double p_zero() const;

  void fix_p0(double p0);

  void set_hillclimbing_mode(HillclimbingMode new_mode);

  void set_hmm_alignments(const HmmWrapper& hmm_wrapper);

  void set_fertility_limit(uint new_limit);

  void write_alignments(const std::string filename) const;

  void write_postdec_alignments(const std::string filename, double thresh);

  double AER();

  double AER(const Storage1D<Math1D::Vector<AlignBaseType> >& alignments);

  double f_measure(double alpha = 0.1);

  double f_measure(const Storage1D<Math1D::Vector<AlignBaseType> >& alignments, double alpha = 0.1);

  double DAE_S();

  double DAE_S(const Storage1D<Math1D::Vector<AlignBaseType> >& alignments);

  const NamedStorage1D<Math1D::Vector<double> >& fertility_prob() const;

  const NamedStorage1D<Math1D::Vector<AlignBaseType> >& best_alignments() const;

  void write_fertilities(std::string filename);

  //improves the passed alignment using hill climbing and
  // returns the probability of the resulting alignment
  virtual long double update_alignment_by_hillclimbing(const Storage1D<uint>& source, const Storage1D<uint>& target, 
						       const SingleLookupTable& lookup, uint& nIter, Math1D::Vector<uint>& fertility,
						       Math2D::Matrix<long double>& expansion_prob,
						       Math2D::Matrix<long double>& swap_prob, Math1D::Vector<AlignBaseType>& alignment)
  = 0;


  virtual long double compute_external_alignment(const Storage1D<uint>& source, const Storage1D<uint>& target,
						 const SingleLookupTable& lookup,
						 Math1D::Vector<AlignBaseType>& alignment);
  
  // <code> start_alignment </code> is used as initialization for hillclimbing and later modified
  // the extracted alignment is written to <code> postdec_alignment </code>
  virtual void compute_external_postdec_alignment(const Storage1D<uint>& source, const Storage1D<uint>& target,
						  const SingleLookupTable& lookup,
						  Math1D::Vector<AlignBaseType>& start_alignment,
						  std::set<std::pair<AlignBaseType,AlignBaseType> >& postdec_alignment,
						  double threshold = 0.25);


  void update_alignments_unconstrained();

  void release_memory();

protected:

  virtual void prepare_external_alignment(const Storage1D<uint>& source, const Storage1D<uint>& target,
					  const SingleLookupTable& lookup,
					  Math1D::Vector<AlignBaseType>& alignment) = 0;


  void print_uncovered_set(uint state) const;

  uint nUncoveredPositions(uint state) const;

  void compute_uncovered_sets(uint nMaxSkips = 4);

  void cover(uint level);

  void visualize_set_graph(std::string filename);

  void compute_coverage_states();

  //no actual hillclimbing done here, we directly compute a Viterbi alignment and its neighbors
  long double simulate_hmm_hillclimbing(const Storage1D<uint>& source, const Storage1D<uint>& target, 
					const SingleLookupTable& lookup, const HmmWrapper& hmm_wrapper,
					Math1D::Vector<uint>& fertility, Math2D::Matrix<long double>& expansion_prob,
					Math2D::Matrix<long double>& swap_prob, Math1D::Vector<AlignBaseType>& alignment);

  void compute_postdec_alignment(const Math1D::Vector<AlignBaseType>& alignment,
				 long double best_prob, const Math2D::Matrix<long double>& expansion_move_prob,
				 const Math2D::Matrix<long double>& swap_move_prob, double threshold,
				 std::set<std::pair<AlignBaseType,AlignBaseType> >& postdec_alignment);

  void update_fertility_prob(const Storage1D<Math1D::Vector<double> >& ffert_count, double min_prob = 1e-8);

  inline void update_fertility_counts(const Storage1D<uint>& target,
				      const Math1D::Vector<AlignBaseType>& best_alignment,
				      const Math1D::NamedVector<uint>& fertility,
				      const Math2D::NamedMatrix<long double>& expansion_move_prob,
				      const long double sentence_prob, const long double inv_sentence_prob,
				      Storage1D<Math1D::Vector<double> >& ffert_count);

  inline void update_dict_counts(const Storage1D<uint>& cur_source, const Storage1D<uint>& cur_target,
				 const SingleLookupTable& cur_lookup, 
				 const Math1D::Vector<AlignBaseType>& best_alignment,
				 const Math2D::NamedMatrix<long double>& expansion_move_prob,
				 const Math2D::NamedMatrix<long double>& swap_move_prob,
				 const long double sentence_prob, const long double inv_sentence_prob,
				 Storage1D<Math1D::Vector<double> >& fwcount);

  inline void update_zero_counts(const Math1D::Vector<AlignBaseType>& best_alignment,
				 const Math1D::NamedVector<uint>& fertility,
				 const Math2D::NamedMatrix<long double>& expansion_move_prob,
				 const long double swap_sum, const long double best_prob,
				 const long double sentence_prob, const long double inv_sentence_prob,
				 double& fzero_count, double& fnonzero_count);

  inline long double swap_mass(const Math2D::NamedMatrix<long double>& swap_move_prob) const;

  inline double common_icm_change(const Math1D::Vector<uint>& cur_fertilities,
				  const double log_pzero, const double log_pnonzero,
				  const Math1D::NamedVector<uint>& dict_sum,
				  const Math1D::Vector<double>& cur_dictcount, const Math1D::Vector<double>& hyp_dictcount,
				  const Math1D::Vector<double>& cur_fert_count, const Math1D::Vector<double>& hyp_fert_count,
				  const uint cur_target_word, const uint hyp_target_word,
				  const uint cur_idx, const uint hyp_idx,
				  const uint cur_aj, const uint hyp_aj, const uint curJ) const;
    
  void common_prepare_external_alignment(const Storage1D<uint>& source, const Storage1D<uint>& target,
					 const SingleLookupTable& lookup, Math1D::Vector<AlignBaseType>& alignment);


  //converts the passed alignment so that it satisfies the constraints on the fertilities, including the one for the empty word.
  // returns if the alignment was changed
  bool make_alignment_feasible(const Storage1D<uint>& source, const Storage1D<uint>& target,
			       const SingleLookupTable& lookup, Math1D::Vector<AlignBaseType>& alignment);



  Math2D::NamedMatrix<ushort> uncovered_set_;
  
  //the first entry denotes the predecessor state, the second the source position covered in the transition
  NamedStorage1D<Math2D::Matrix<uint> > predecessor_sets_;

  //tells for each state how many uncovered positions are in that state
  Math1D::NamedVector<ushort> nUncoveredPositions_; 
  Math1D::NamedVector<ushort> j_before_end_skips_;

  //first_set_[i] marks the first row of <code> uncovered_sets_ </code> where the
  // position i appears
  Math1D::NamedVector<uint> first_set_;
  uint next_set_idx_; // used during the computation of uncovered sets

  //each row is a coverage state, where the first index denotes the number of uncovered set and the
  //second the maximum covered source position
  Math2D::NamedMatrix<uint> coverage_state_;
  Math1D::NamedVector<uint> first_state_;
  NamedStorage1D<Math2D::Matrix<uint> > predecessor_coverage_states_;
  

  const Storage1D<Storage1D<uint> >& source_sentence_;
  const LookupTable& slookup_;
  const Storage1D<Storage1D<uint> >& target_sentence_;

  const CooccuringWordsType& wcooc_;
  SingleWordDictionary& dict_;

  uint nSourceWords_;
  uint nTargetWords_;

  uint maxJ_;
  uint maxI_;

  uint fertility_limit_;

  double p_zero_;
  double p_nonzero_;

  bool fix_p0_;

  uint iter_offs_;

  bool och_ney_empty_word_;
  bool smoothed_l0_;
  double l0_beta_;
  double l0_fertpen_;

  bool no_factorial_;

  HillclimbingMode hillclimb_mode_; 

  const floatSingleWordDictionary& prior_weight_;

  NamedStorage1D<Math1D::Vector<double> > fertility_prob_;

  NamedStorage1D<Math1D::Vector<AlignBaseType> > best_known_alignment_;

  std::map<uint,std::set<std::pair<AlignBaseType,AlignBaseType> > > sure_ref_alignments_;
  std::map<uint,std::set<std::pair<AlignBaseType,AlignBaseType> > > possible_ref_alignments_;

  Math1D::Vector<long double> ld_fac_; //precomputation of factorials

  const Math1D::Vector<double>& log_table_;
};


/*************** definition of inline functions ************/

inline void FertilityModelTrainer::update_fertility_counts(const Storage1D<uint>& target,
							   const Math1D::Vector<AlignBaseType>& best_alignment,
							   const Math1D::NamedVector<uint>& fertility,
							   const Math2D::NamedMatrix<long double>& expansion_move_prob,
							   const long double sentence_prob, const long double inv_sentence_prob,
							   Storage1D<Math1D::Vector<double> >& ffert_count) {


  uint curJ = best_alignment.size();
  uint curI = target.size();

  assert(fertility.size() == curI+1);

  //this passage exploits that entries of expansion_move_prob are 0.0 if they refer to an unchanged alignment
  
  for (uint i=1; i <= curI; i++) {

    const uint t_idx = target[i-1];

    Math1D::Vector<double>& cur_fert_count = ffert_count[t_idx];
    
    const uint cur_fert = fertility[i];
	  
    long double addon = sentence_prob;
    for (uint j=0; j < curJ; j++) {
      if (best_alignment[j] == i) {
	for (uint ii=0; ii <= curI; ii++)
	  addon -= expansion_move_prob(j,ii);
      }
      else
	addon -= expansion_move_prob(j,i);
    }
    addon *= inv_sentence_prob;
      
    double daddon = (double) addon;
    if (!(daddon > 0.0)) {
      std::cerr << "STRANGE: fractional weight " << daddon << " for sentence pair with "
		<< curJ << " source words and " << curI << " target words" << std::endl;
      std::cerr << "sentence prob: " << sentence_prob << std::endl;
      std::cerr << "" << std::endl;
    }
      
    cur_fert_count[cur_fert] += addon;

    //NOTE: swap moves do not change the fertilities
    if (cur_fert > 0) {
      long double alt_addon = 0.0;
      for (uint j=0; j < curJ; j++) {
	if (best_alignment[j] == i) {
	  for (uint ii=0; ii <= curI; ii++) {
	    alt_addon += expansion_move_prob(j,ii);
	  }
	}
      }
      
      cur_fert_count[cur_fert-1] += inv_sentence_prob * alt_addon;
    }

    //check for the unlikely event that all source words in best_alignment align to i
    if (cur_fert+1 < fertility_prob_[t_idx].size()) {
      
      long double alt_addon = 0.0;
      for (uint j=0; j < curJ; j++) {
	alt_addon += expansion_move_prob(j,i);
      }
	
      cur_fert_count[cur_fert+1] += inv_sentence_prob * alt_addon;
    }
  }

}



inline void FertilityModelTrainer::update_dict_counts(const Storage1D<uint>& cur_source, const Storage1D<uint>& cur_target,
						      const SingleLookupTable& cur_lookup, 
						      const Math1D::Vector<AlignBaseType>& best_alignment,
						      const Math2D::NamedMatrix<long double>& expansion_move_prob,
						      const Math2D::NamedMatrix<long double>& swap_move_prob,
						      const long double sentence_prob, const long double inv_sentence_prob,
						      Storage1D<Math1D::Vector<double> >& fwcount) {

  const uint curJ = cur_source.size();
  const uint curI = cur_target.size();

  for (uint j=0; j < curJ; j++) {

    const uint s_idx = cur_source[j];
    const uint cur_aj = best_alignment[j];
    
    //this passage exploits that entries of expansion_move_prob and swap_move_prob are 0.0 if they refer to an unchanged alignment

    long double addon = sentence_prob;
    for (uint i=0; i <= curI; i++)
      addon -= expansion_move_prob(j,i);
    for (uint jj=0; jj < curJ; jj++)
      addon -= swap_move_prob(j,jj); //exploits that swap_move_prob is a symmetric matrix
    
    addon *= inv_sentence_prob;
    if (cur_aj != 0) {
      fwcount[cur_target[cur_aj-1]][cur_lookup(j,cur_aj-1)] += addon;
    }
    else {
      fwcount[0][s_idx-1] += addon;
    }
    
    for (uint i=0; i <= curI; i++) {
      
      if (i != cur_aj) {

	const uint t_idx = (i == 0) ? 0 : cur_target[i-1];
	
	long double addon = expansion_move_prob(j,i);
	for (uint jj=0; jj < curJ; jj++) {
	  if (best_alignment[jj] == i)
	    addon += swap_move_prob(j,jj);
	}
	addon *= inv_sentence_prob;
	
	if (i!=0) {
	  fwcount[t_idx][cur_lookup(j,i-1)] += addon;
	}
	else {
	  fwcount[0][s_idx-1] += addon;
	}
      }
    }
  }

}


inline void FertilityModelTrainer::update_zero_counts(const Math1D::Vector<AlignBaseType>& best_alignment,
						      const Math1D::NamedVector<uint>& fertility,
						      const Math2D::NamedMatrix<long double>& expansion_move_prob,
						      const long double swap_sum, const long double best_prob,
						      const long double sentence_prob, const long double inv_sentence_prob,
						      double& fzero_count, double& fnonzero_count) {

  const uint curJ = expansion_move_prob.xDim();
  const uint curI = expansion_move_prob.yDim()-1;

  assert(curJ == best_alignment.size());
  assert(fertility.size() == curI+1);

  double cur_zero_weight = best_prob;
  cur_zero_weight += swap_sum; 
  for (uint j=0; j < curJ; j++) {
    if (best_alignment[j] != 0) {
      for (uint i=1; i <= curI; i++)
	cur_zero_weight += expansion_move_prob(j,i);
    }
  }

  cur_zero_weight *= inv_sentence_prob;
  
  assert(!isnan(cur_zero_weight));
  assert(!isinf(cur_zero_weight));
  
  fzero_count += cur_zero_weight * (fertility[0]);
  fnonzero_count += cur_zero_weight * (curJ - 2*fertility[0]);

  if (curJ >= 2*(fertility[0]+1)) {
    long double inc_zero_weight = 0.0;
    for (uint j=0; j < curJ; j++)
      inc_zero_weight += expansion_move_prob(j,0);
    
    inc_zero_weight *= inv_sentence_prob;
    fzero_count += inc_zero_weight * (fertility[0]+1);
    fnonzero_count += inc_zero_weight * (curJ -2*(fertility[0]+1));
    
    assert(!isnan(inc_zero_weight));
    assert(!isinf(inc_zero_weight));
  }
  
  if (fertility[0] > 1) {
    long double dec_zero_weight = 0.0;
    for (uint j=0; j < curJ; j++) {
      if (best_alignment[j] == 0) {
	for (uint i=1; i <= curI; i++)
	  dec_zero_weight += expansion_move_prob(j,i);
      }
    }
    
    dec_zero_weight *= inv_sentence_prob;
    
    fzero_count += dec_zero_weight * (fertility[0]-1);
    fnonzero_count += dec_zero_weight * (curJ -2*(fertility[0]-1));
    
    assert(!isnan(dec_zero_weight));
    assert(!isinf(dec_zero_weight));
  }

  //DEBUG
  if (isnan(fzero_count) || isnan(fnonzero_count)
      || isinf(fzero_count) || isinf(fnonzero_count) ) {
    
    std::cerr << "zero counts: " << fzero_count << ", " << fnonzero_count << std::endl;
    std::cerr << "J=" << curJ << ", I=" << curI << std::endl;
    std::cerr << "sentence weight: " << sentence_prob << std::endl;
    exit(1);
  }
  //END_DEBUG
}

inline long double FertilityModelTrainer::swap_mass(const Math2D::NamedMatrix<long double>& swap_move_prob) const {

  //return 0.5 * swap_move_prob.sum();

  const uint J = swap_move_prob.xDim();
  assert(J == swap_move_prob.yDim());

  double sum = 0.0;
  for (uint j1 = 0; j1 < J-1; j1++) 
    for (uint j2 = j1+1; j2 < J; j2++)
      sum += swap_move_prob(j1,j2);

  return sum;
}

inline double FertilityModelTrainer::common_icm_change(const Math1D::Vector<uint>& cur_fertilities,
						       const double log_pzero, const double log_pnonzero,
						       const Math1D::NamedVector<uint>& dict_sum,
						       const Math1D::Vector<double>& cur_dictcount,
						       const Math1D::Vector<double>& hyp_dictcount,
						       const Math1D::Vector<double>& cur_fert_count,
						       const Math1D::Vector<double>& hyp_fert_count,
						       const uint cur_word, const uint new_target_word,
						       const uint cur_idx, const uint hyp_idx,
						       const uint cur_aj, const uint hyp_aj,
						       const uint curJ) const {


  double change = 0.0;


  const uint cur_fert = cur_fertilities[cur_aj];
  const uint cur_hyp_fert = cur_fertilities[hyp_aj];


  if (cur_word != new_target_word) {
    
    uint cur_dictsum = dict_sum[cur_word];
    
    if (dict_sum[new_target_word] > 0)
      change -= double(dict_sum[new_target_word]) * log_table_[ dict_sum[new_target_word] ];
    change += double(dict_sum[new_target_word]+1) * log_table_[ dict_sum[new_target_word]+1 ];
		
    if (hyp_dictcount[hyp_idx] > 0)
      change += double(hyp_dictcount[hyp_idx]) * 
	log_table_[hyp_dictcount[hyp_idx]];
    else
      change += prior_weight_[new_target_word][hyp_idx]; 
    
    change -= double(hyp_dictcount[hyp_idx]+1) * 
      log_table_[hyp_dictcount[hyp_idx]+1];
    
    change -= double(cur_dictsum) * log_table_[cur_dictsum];
    if (cur_dictsum > 1)
      change += double(cur_dictsum-1) * log_table_[cur_dictsum-1];
    
    change -= - double(cur_dictcount[cur_idx]) * log_table_[cur_dictcount[cur_idx]];
    
    
    if (cur_dictcount[cur_idx] > 1) {
      change += double(cur_dictcount[cur_idx]-1) * (-log_table_[cur_dictcount[cur_idx]-1]);
    }
    else
      change -= prior_weight_[cur_word][cur_idx];

    
    /***** fertilities for the (easy) case where the old and the new word differ ****/
		
    //note: currently not updating f_zero / f_nonzero
    if (cur_aj == 0) {
      
      const uint zero_fert = cur_fert;
      const uint new_zero_fert = zero_fert-1;
      
      
      //changes regarding ldchoose()
      change -= log_table_[curJ-new_zero_fert];
      change += log_table_[zero_fert]; // - - = +
      change += log_table_[curJ-2*zero_fert+1] + log_table_[curJ-2*new_zero_fert];
		  
      change += log_pzero; // - -  = +

      if (och_ney_empty_word_) {
	
	change -= log_table_[curJ] - log_table_[zero_fert];
      }
		
      change -= 2.0*log_pnonzero;
    }
    else {

      if (!no_factorial_)
	change -= - log_table_[cur_fert];
		  
      const int c = cur_fert_count[cur_fert];
      change -= -c * log_table_[c];
      if (c > 1)
	change += -(c-1) * log_table_[c-1];
      
      const int c2 = cur_fert_count[cur_fert-1];
		  
      if (c2 > 0)
	change -= -c2 * log_table_[c2];
      change += -(c2+1) * log_table_[c2+1];
    }
    
    if (hyp_aj == 0) {
		  
      const uint zero_fert = cur_hyp_fert;
      const uint new_zero_fert = zero_fert+1;

      //changes regarding ldchoose()
      change += log_table_[curJ-zero_fert]; // - -  = +
      change -= log_table_[curJ-2*zero_fert] + log_table_[curJ-2*zero_fert-1];
      change += log_table_[new_zero_fert]; 

      change += 2.0*log_pnonzero;

      change -= log_pzero;
		  
      if (och_ney_empty_word_) {
	
	change += log_table_[curJ] - log_table_[new_zero_fert];
      }
    }
    else {
      
      if (!no_factorial_)
	change += - log_table_[cur_hyp_fert+1]; 
      
      const int c = hyp_fert_count[cur_hyp_fert];
      change -= -c * log_table_[c];
      if (c > 1)
	change += -(c-1) * log_table_[c-1];
      else
	change -= l0_fertpen_;
      
      const int c2 = hyp_fert_count[cur_hyp_fert+1];
      if (c2 > 0)
	change -= -c2 * log_table_[c2];
      else
	change += l0_fertpen_;
      change += -(c2+1) * log_table_[c2+1];
    }
  }
  else {
    //the old and the new word are the same. 
    //No dictionary terms affected, but the fertilities are tricky in this case
    
    assert(cur_aj != 0);
    assert(hyp_aj != 0);

    if (!no_factorial_) {

      change += log_table_[cur_fert]; // - - = +
      change -= log_table_[cur_hyp_fert+1];
    }
		
    const Math1D::Vector<double>& cur_count = cur_fert_count;
    Math1D::Vector<double> new_count = cur_count;
    new_count[cur_fert]--;
    new_count[cur_fert-1]++;
    new_count[cur_hyp_fert]--;
    new_count[cur_hyp_fert+1]++;
    
    for (uint k=0; k < cur_count.size(); k++) {
      if (cur_count[k] != new_count[k]) {
	change += cur_count[k] * log_table_[cur_count[k]]; // - - = +
	change -= new_count[k] * log_table_[new_count[k]];
      }
    }
  }

  return change;
}


#endif
