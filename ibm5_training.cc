/**** written by Thomas Schoenemann as a private person without employment, February 2013 ****/

#include "ibm5_training.hh"

#include "combinatoric.hh"
#include "timing.hh"
#include "projection.hh"
#include "training_common.hh" // for get_wordlookup(), dictionary and start-prob m-step 
#include "stl_util.hh"
#include "alignment_computation.hh"

#ifdef HAS_GZSTREAM
#include "gzstream.h"
#endif

#include <fstream>
#include <set>
#include "stl_out.hh"

IBM5Trainer::IBM5Trainer(const Storage1D<Storage1D<uint> >& source_sentence,
			 const LookupTable& slookup,
			 const Storage1D<Storage1D<uint> >& target_sentence,
			 const std::map<uint,std::set<std::pair<AlignBaseType,AlignBaseType> > >& sure_ref_alignments,
			 const std::map<uint,std::set<std::pair<AlignBaseType,AlignBaseType> > >& possible_ref_alignments,
			 SingleWordDictionary& dict,
			 const CooccuringWordsType& wcooc,
			 uint nSourceWords, uint nTargetWords,
			 const floatSingleWordDictionary& prior_weight,
			 const Storage1D<WordClassType>& source_class,
			 const Storage1D<WordClassType>& target_class,
			 const Math1D::Vector<double>& log_table,
			 bool use_sentence_start_prob, bool nonpar_distortion, 
			 IBM4CeptStartMode cept_start_mode, IBM4IntraDistMode intra_dist_mode,
			 bool smoothed_l0, double l0_beta, double l0_fertpen) 
: FertilityModelTrainer(source_sentence,slookup,target_sentence,dict,wcooc,nSourceWords,nTargetWords,
			prior_weight, false, smoothed_l0, l0_beta, l0_fertpen, true,
			sure_ref_alignments,possible_ref_alignments,log_table),
  inter_distortion_prob_(maxJ_+1), intra_distortion_prob_(maxJ_+1), sentence_start_prob_(maxJ_+1),
  nonpar_distortion_(nonpar_distortion), use_sentence_start_prob_(use_sentence_start_prob), 
  cept_start_mode_(cept_start_mode), intra_dist_mode_(intra_dist_mode), 
  source_class_(source_class), target_class_(target_class)
{

  uint max_source_class = 0;
  uint min_source_class = MAX_UINT;
  for (uint j=1; j < source_class_.size(); j++) {
    max_source_class = std::max<uint>(max_source_class,source_class_[j]);
    min_source_class = std::min<uint>(min_source_class,source_class_[j]);
  }
  if (min_source_class > 0) {
    for (uint j=1; j < source_class_.size(); j++) 
      source_class_[j] -= min_source_class;
    max_source_class -= min_source_class;
  }

  nSourceClasses_ = max_source_class+1;

  uint max_target_class = 0;
  uint min_target_class = MAX_UINT;
  for (uint i=1; i < target_class_.size(); i++) {
    max_target_class = std::max<uint>(max_target_class,target_class_[i]);
    min_target_class = std::min<uint>(min_target_class,target_class_[i]);
  }
  if (min_target_class > 0) {
    for (uint i=1; i < target_class_.size(); i++)
      target_class_[i] -= min_target_class;
    max_target_class -= min_target_class;
  }

  nTargetClasses_ = max_target_class+1;

  for (uint J=1; J <= maxJ_; J++) {
    //the second index has an offset of 1, but one position must already be taken
    inter_distortion_prob_[J].resize(J,maxJ_,nSourceClasses_,1.0/J); 
  }
  inter_distortion_param_.resize(2*maxJ_+1,nSourceClasses_,1.0 / (2*maxJ_+1));

  displacement_offset_ = maxJ_;

  const uint nClasses = (intra_dist_mode == IBM4IntraDistModeSource) ? nSourceClasses_ : nTargetClasses_;

  intra_distortion_param_.resize(maxJ_,nClasses,1.0 / maxJ_);
  for (uint J=1; J <= maxJ_; J++)
    intra_distortion_prob_[J].resize(J,nClasses,1.0 / J);


  std::set<uint> seenJs;
  for (uint s=0; s < source_sentence.size(); s++)
    seenJs.insert(source_sentence[s].size());

  sentence_start_parameters_.resize(maxJ_,1.0 / maxJ_);

  for (uint J=1; J <= maxJ_; J++) {
    if (seenJs.find(J) != seenJs.end()) {
      sentence_start_prob_[J].resize(J,1.0 / J);
    }
  }

}

/*virtual*/ std::string IBM5Trainer::model_name() const {

  return "IBM-5";
}

void IBM5Trainer::par2nonpar_inter_distortion() {

  for (uint J=1; J < inter_distortion_prob_.size(); J++) {
    
    for (uint s=0; s < inter_distortion_prob_[J].zDim(); s++) {
   
      for (uint prev_pos=0; prev_pos < inter_distortion_prob_[J].yDim(); prev_pos++) {

	double denom = 0.0;
	for (uint j=0; j < J; j++)
	  denom += inter_distortion_param_(displacement_offset_+j-prev_pos,s);

	assert(denom > 1e-305);
	
	for (uint j=0; j < J; j++)
	  inter_distortion_prob_[J](j,prev_pos,s) = 
	    std::max(1e-8, inter_distortion_param_(displacement_offset_+j-prev_pos,s) / denom);
      }
    }
  }
}

void IBM5Trainer::par2nonpar_intra_distortion() {

  for (uint J=1; J < intra_distortion_prob_.size(); J++) {

    for (uint s=0; s < intra_distortion_prob_[J].yDim(); s++) {

      double sum = 0.0;
      for (uint j=0; j < intra_distortion_prob_[J].xDim(); j++)
	sum += intra_distortion_param_(j,s);

      assert(sum > 1e-305);

      for (uint j=0; j < intra_distortion_prob_[J].xDim(); j++)
	intra_distortion_prob_[J](j,s) = intra_distortion_param_(j,s) / sum;
    }
  }
}

long double IBM5Trainer::alignment_prob(const Storage1D<uint>& source, const Storage1D<uint>& target, 
					const SingleLookupTable& lookup, const Math1D::Vector<AlignBaseType>& alignment) {

  long double prob = 1.0;

  const uint curI = target.size();
  const uint curJ = source.size();

  assert(alignment.size() == curJ);

  Math1D::NamedVector<uint> fertility(curI+1,0,MAKENAME(fertility));
  Storage1D<std::vector<ushort> > aligned_source_words(curI+1);

  for (uint j=0; j < curJ; j++) {
    const uint aj = alignment[j];
    aligned_source_words[aj].push_back(j);
    fertility[aj]++;
    
    if (aj == 0) {
      prob *= dict_[0][source[j]-1];
      //DEBUG
      if (isnan(prob))
        std::cerr << "prob nan after empty word dict prob" << std::endl;
      //END_DEBUG
    }
    else {
      const uint ti = target[aj-1];
      prob *= dict_[ti][lookup(j,aj-1)];
    }
  }
  
  if (curJ < 2*fertility[0])
    return 0.0;

  for (uint i=1; i <= curI; i++) {
    uint t_idx = target[i-1];
    prob *= fertility_prob_[t_idx][fertility[i]];
  }

  //DEBUG
  if (isnan(prob))
    std::cerr << "prob nan after fertility probs" << std::endl;
  //END_DEBUG

  prob *= distortion_prob(source,target,aligned_source_words);

  //handle empty word -- dictionary probs were handled above
  assert(fertility[0] <= 2*curJ);
  
  prob *= ldchoose(curJ-fertility[0],fertility[0]);
  for (uint k=1; k <= fertility[0]; k++)
    prob *= p_zero_;
  for (uint k=1; k <= curJ-2*fertility[0]; k++)
    prob *= p_nonzero_;

  assert(!isnan(prob));

  return prob;
}

//NOTE: the vectors need to be sorted
long double IBM5Trainer::distortion_prob(const Storage1D<uint>& source, const Storage1D<uint>& target, 
					 const Storage1D<std::vector<AlignBaseType> >& aligned_source_words) {

  const uint J = source.size();

  long double prob = 1.0;

  uint prev_center = MAX_UINT;

  Storage1D<bool> fixed(J,false);

  uint nOpen = J;

  for (uint i = 1; i < aligned_source_words.size(); i++) {

    const std::vector<AlignBaseType>& cur_aligned_source_words = aligned_source_words[i];

    if (cur_aligned_source_words.size() > 0) {

      const uint first_j = cur_aligned_source_words[0];

      //a) head of the cept
      if (prev_center == MAX_UINT) {

	assert(cept_start_mode_ == IBM4UNIFORM || nOpen == J);
	
	if (cept_start_mode_ == IBM4UNIFORM)
	  prob *= 1.0 / nOpen;
	else
	  prob *= sentence_start_prob_[J][first_j];
      }
      else {

	const uint sclass = source_class_[source[first_j]];

	const uint nAvailable = nOpen - (cur_aligned_source_words.size()-1);

	const Math3D::Tensor<double>& cur_inter_distortion = inter_distortion_prob_[nAvailable];

	uint pos_first_j = MAX_UINT;
	uint pos_prev_center = MAX_UINT;

	uint nCurOpen = 0;
	for (uint j = 0; j <= std::max(first_j,prev_center); j++) {

	  if (j == first_j)
	    pos_first_j = nCurOpen;
	  
	  if (!fixed[j])
	    nCurOpen++;

	  if (j == prev_center)
	    pos_prev_center = nCurOpen;
	}

	//DEBUG
	if (pos_prev_center >= cur_inter_distortion.yDim()) {

	  std::cerr << "J= " << J << ", prev_center=" << prev_center << ", pos_prev_center = " << pos_prev_center << std::endl;
	  std::cerr << "fixed: " << fixed << std::endl;
	}
	//END_DEBUG

	assert(pos_prev_center < cur_inter_distortion.yDim());

	prob *= cur_inter_distortion(pos_first_j,pos_prev_center,sclass);
      }
	
      fixed[first_j] = true;
      nOpen--;

      //b) body of the cept
      for (uint k=1; k < cur_aligned_source_words.size(); k++) {

	uint cur_j = cur_aligned_source_words[k];

	const uint cur_class = (intra_dist_mode_ == IBM4IntraDistModeSource) ? source_class_[source[cur_j]]
	  : target_class_[target[i-1]];

	uint pos = MAX_UINT;

	uint nAvailable = 0;
	for (uint j = cur_aligned_source_words[k-1]+1; j < J; j++) {
	  
	  if (j == cur_j)
	    pos = nAvailable;

	  if (!fixed[j])
	    nAvailable++;
	}

	nAvailable -= cur_aligned_source_words.size() - 1 - k;

	prob *= intra_distortion_prob_[nAvailable](pos,cur_class);

	fixed[cur_j] = true;
	nOpen--;
      }
      
      //c) calculate the center of the cept
      switch (cept_start_mode_) {
      case IBM4CENTER : {

	//compute the center of this cept and store the result in prev_cept_center
	double sum = 0.0;
	for (uint k=0; k < cur_aligned_source_words.size(); k++) {
	  sum += cur_aligned_source_words[k];
	}

        prev_center = (int) round(sum / cur_aligned_source_words.size());
        break;
      }
      case IBM4FIRST:
        prev_center = first_j;
        break;
      case IBM4LAST:
        prev_center = cur_aligned_source_words.back();
        break;
      case IBM4UNIFORM:
        break;
      default:
        assert(false);
      }
      
    }
  }

  assert(!isnan(prob));

  return prob;
}

void IBM5Trainer::init_from_ibm4(IBM4Trainer& ibm4, bool count_collection, bool viterbi) {

  for (size_t s=0; s < source_sentence_.size(); s++) 
    best_known_alignment_[s] = ibm4.best_alignments()[s];

  if (!fix_p0_) {
    p_zero_ = ibm4.p_zero();
    p_nonzero_ = 1.0 - p_zero_;
  }

  if (use_sentence_start_prob_ && ibm4.sentence_start_parameters().size() == sentence_start_parameters_.size()) {

    sentence_start_parameters_ = ibm4.sentence_start_parameters();
    par2nonpar_start_prob(sentence_start_parameters_,sentence_start_prob_);
  }

  if (count_collection) {

    if (!viterbi)
      train_unconstrained(1,&ibm4);
    else
      train_viterbi(1,&ibm4);
  }
  else {

    for (uint k=1; k < fertility_prob_.size(); k++) {
      fertility_prob_[k] = ibm4.fertility_prob()[k];
      
      //EXPERIMENTAL
      for (uint l=0; l < fertility_prob_[k].size(); l++) {
	if (l <= fertility_limit_)
	  fertility_prob_[k][l] = 0.95 * fertility_prob_[k][l] 
	    + 0.05 / std::min<uint>(fertility_prob_[k].size(),fertility_limit_);
	else
	  fertility_prob_[k][l] = 0.95 * fertility_prob_[k][l];
      }
      //END_EXPERIMENTAL
    }


    //init distortion models from best known alignments
    Storage1D<Math3D::Tensor<double> > inter_distortion_count = inter_distortion_prob_;
    Math2D::Matrix<double> inter_distparam_count = inter_distortion_param_;
    
    Storage1D<Math2D::Matrix<double> > intra_distortion_count = intra_distortion_prob_;
    Math2D::Matrix<double> intra_distparam_count = intra_distortion_param_;
    
    /*** clear counts ***/
    for (uint J=1; J < inter_distortion_count.size(); J++)
      inter_distortion_count[J].set_constant(0.0);
    inter_distparam_count.set_constant(0.0);
    
    for (uint J=1; J < intra_distortion_count.size(); J++)
      intra_distortion_count[J].set_constant(0.0);
    intra_distparam_count.set_constant(0.0);
    
    for (uint s=0; s < source_sentence_.size(); s++) {
      
      const Storage1D<uint>& cur_source = source_sentence_[s];
      const Storage1D<uint>& cur_target = target_sentence_[s];
      
      const uint curI = cur_target.size();
      const uint curJ = cur_source.size();
      
      NamedStorage1D<std::vector<AlignBaseType> > aligned_source_words(curI+1,MAKENAME(aligned_source_words));
      
      for (uint j=0; j < curJ; j++) {
	const uint aj = best_known_alignment_[s][j];
        aligned_source_words[aj].push_back(j);
      }
    

      {

	uint prev_center = MAX_UINT;

	Storage1D<bool> fixed(curJ,false);

	uint nOpen = curJ;

	for (uint i=1; i <= curI; i++) {
	  
	  if (aligned_source_words[i].size() > 0) {
	   
	    const std::vector<ushort>& cur_aligned_source_words = aligned_source_words[i];

	    //a) head of the cept
	    const uint first_j = cur_aligned_source_words[0];

	    if (prev_center != MAX_UINT) { // currently not estimating a start probability
	      
	      const uint sclass = source_class_[cur_source[first_j]];

	      const uint nAvailable = nOpen - (cur_aligned_source_words.size()-1);

	      Math3D::Tensor<double>& cur_inter_distortion_count = inter_distortion_count[nAvailable];

	      uint pos_first_j = MAX_UINT;
	      uint pos_prev_center = MAX_UINT;
	      
	      uint nCurOpen = 0;
	      for (uint j = 0; j <= std::max(first_j,prev_center); j++) {
		
		if (j == first_j)
		  pos_first_j = nCurOpen;
		
		if (!fixed[j])
		  nCurOpen++;
		
		if (j == prev_center)
		  pos_prev_center = nCurOpen;
	      }
	      
	      cur_inter_distortion_count(pos_first_j,pos_prev_center,sclass) += 1.0;
	      inter_distparam_count(displacement_offset_+pos_first_j-pos_prev_center,sclass) += 1.0;
	    }

	    fixed[first_j] = true;
	    nOpen--;

	    //b) body of the cept
	    for (uint k=1; k < cur_aligned_source_words.size(); k++) {

	      const uint cur_j = cur_aligned_source_words[k];

	      const uint sclass = source_class_[cur_source[cur_j]];

	      uint pos = MAX_UINT;

	      uint nAvailable = 0;
	      for (uint j = cur_aligned_source_words[k-1]+1; j < curJ; j++) {
		
		if (j == cur_j)
		  pos = nAvailable;
		
		if (!fixed[j])
		  nAvailable++;
	      }

	      nAvailable -= cur_aligned_source_words.size() - 1 - k;
	      
	      intra_distortion_count[nAvailable](pos,sclass) += 1.0;
	      intra_distparam_count(pos,sclass) += 1.0;

	      fixed[cur_j] = true;
	      nOpen--;
	    }

	    //c) calculate the center of the cept
	    switch (cept_start_mode_) {
	    case IBM4CENTER : {
	      
	      //compute the center of this cept and store the result in prev_cept_center
	      double sum = 0.0;
	      for (uint k=0; k < cur_aligned_source_words.size(); k++) {
		sum += cur_aligned_source_words[k];
	      }
	      
	      prev_center = (int) round(sum / cur_aligned_source_words.size());
	      break;
	    }
	    case IBM4FIRST:
	      prev_center = first_j;
	      break;
	    case IBM4LAST:
	      prev_center = cur_aligned_source_words.back();
	      break;
	    case IBM4UNIFORM:
	      break;
	    default:
	      assert(false);
	    }

	  }
	}
      }      

    }

    
    //update inter distortion probabilities
    if (nonpar_distortion_) {

      for (uint J=1; J < inter_distortion_count.size(); J++) {

	for (uint y=0; y < inter_distortion_count[J].yDim(); y++) {
	  for (uint z=0; z < inter_distortion_count[J].zDim(); z++) {
	    
	    double sum = 0.0;
	    for (uint j=0; j < inter_distortion_count[J].xDim(); j++)
	      sum += inter_distortion_count[J](j,y,z);
	    
	    if (sum > 1e-305) {
	      
	      for (uint j=0; j < inter_distortion_count[J].xDim(); j++)
		inter_distortion_prob_[J](j,y,z) = 0.95 * std::max(1e-8,inter_distortion_count[J](j,y,z) / sum)
		  + 0.05 * inter_distortion_prob_[J](j,y,z);
	    }
	  }
	}
      }
    }
    else {

      for (uint s=0; s < inter_distortion_param_.yDim(); s++) {

	double sum = 0.0;
	for (uint j=0; j < inter_distortion_param_.xDim(); j++)
	  sum += inter_distparam_count(j,s);
	
	assert(sum > 1e-305);
	
	for (uint j=0; j < inter_distortion_param_.xDim(); j++)
	  inter_distortion_param_(j,s) = 0.95 * (inter_distparam_count(j,s) / sum) 
	    + 0.05 * inter_distortion_param_(j,s);
      }

      par2nonpar_inter_distortion();

    }    

    //update intra distortion probabilities
    if (nonpar_distortion_) {

      for (uint J=1; J < intra_distortion_prob_.size(); J++) {
	
	for (uint s=0; s < intra_distortion_prob_[J].yDim(); s++) {

	  double sum = 0.0;
	  for (uint j=0; j < J; j++)
	    sum += intra_distortion_count[J](j,s);
	  
	  if (sum > 1e-305) {
	    for (uint j=0; j < J; j++)
	      intra_distortion_prob_[J](j,s) = 0.95 * std::max(1e-8,intra_distortion_count[J](j,s) / sum)
		+ 0.05 * intra_distortion_prob_[J](j,s);
	  }
	}
      }
    }
    else {

      for (uint s=0; s < intra_distparam_count.yDim(); s++) {

	double sum = 0.0;
	for (uint j=0; j < intra_distparam_count.xDim(); j++)
	  sum += intra_distparam_count(j,s);
	
	assert(sum > 1e-305);
      
	for (uint j=0; j < intra_distparam_count.xDim(); j++)
	  intra_distortion_param_(j,s) = 0.95 * (intra_distparam_count(j,s) / sum)
	    + 0.05 * intra_distortion_param_(j,s);
      }
	
      par2nonpar_intra_distortion();
    }

  }
}

long double IBM5Trainer::update_alignment_by_hillclimbing(const Storage1D<uint>& source, const Storage1D<uint>& target, 
							  const SingleLookupTable& lookup, uint& nIter, Math1D::Vector<uint>& fertility,
							  Math2D::Matrix<long double>& expansion_prob,
							  Math2D::Matrix<long double>& swap_prob, Math1D::Vector<AlignBaseType>& alignment) {

  /**** calculate probability of the passed alignment *****/

  double improvement_factor = 1.001;
  
  const uint curI = target.size();
  const uint curJ = source.size();

  Storage1D<std::vector<AlignBaseType> > aligned_source_words(curI+1);

  fertility.set_constant(0);

  for (uint j=0; j < curJ; j++) {

    const uint aj = alignment[j];

    aligned_source_words[aj].push_back(j);

    fertility[aj]++;
  }

  long double base_distortion_prob = distortion_prob(source,target,aligned_source_words);
  long double base_prob = base_distortion_prob;

  for (uint i=1; i <= curI; i++) {
    uint t_idx = target[i-1];
    //NOTE: no factorial here 
    base_prob *= fertility_prob_[t_idx][fertility[i]];
  }
  for (uint j=0; j < curJ; j++) {
    
    uint s_idx = source[j];
    uint aj = alignment[j];
    
    if (aj == 0)
      base_prob *= dict_[0][s_idx-1];
    else {
      uint t_idx = target[aj-1];
      base_prob *= dict_[t_idx][lookup(j,aj-1)]; 
    }
  }

  base_prob *= ldchoose(curJ-fertility[0],fertility[0]);
  for (uint k=1; k <= fertility[0]; k++)
    base_prob *= p_zero_;
  for (uint k=1; k <= curJ-2*fertility[0]; k++)
    base_prob *= p_nonzero_;

  //DEBUG
#ifndef NDEBUG
  assert(!isnan(base_prob));
  long double check_prob = alignment_prob(source,target,lookup,alignment);
  long double check_ratio = base_prob / check_prob;
  assert(check_ratio >= 0.99 && check_ratio <= 1.01);
#endif
  //END_DEBUG


  uint count_iter = 0;

  //Math1D::Vector<AlignBaseType> hyp_alignment = alignment;
  Storage1D<std::vector<AlignBaseType> > hyp_aligned_source_words = aligned_source_words;

  swap_prob.resize(curJ,curJ);
  expansion_prob.resize(curJ,curI+1);

  while (true) {    

    count_iter++;
    nIter++;

    //std::cerr << "****************** starting new nondef hc iteration, current best prob: " << base_prob << std::endl;

    const uint zero_fert = fertility[0];

    long double empty_word_increase_const = 0.0;
    if (curJ >= 2*(zero_fert+1)) {

      empty_word_increase_const = (curJ-2*zero_fert) * (curJ - 2*zero_fert-1) * p_zero_ 
        / ((curJ-zero_fert) * (zero_fert+1) * p_nonzero_ * p_nonzero_);

#ifndef NDEBUG
      long double old_const = ldchoose(curJ-zero_fert-1,zero_fert+1) * p_zero_ 
        / (ldchoose(curJ-zero_fert,zero_fert) * p_nonzero_ * p_nonzero_);

      long double ratio = empty_word_increase_const / old_const;

      assert(ratio >= 0.975 && ratio <= 1.05);
#endif      
    }

    long double empty_word_decrease_const = 0.0;
    if (zero_fert > 0) {

      empty_word_decrease_const = (curJ-zero_fert+1) * zero_fert * p_nonzero_ * p_nonzero_ 
        / ((curJ-2*zero_fert+1) * (curJ-2*zero_fert+2) * p_zero_);

#ifndef NDEBUG
      long double old_const = ldchoose(curJ-zero_fert+1,zero_fert-1) * p_nonzero_ * p_nonzero_ 
	/ (ldchoose(curJ-zero_fert,zero_fert) * p_zero_);

      long double ratio = empty_word_decrease_const / old_const;

      assert(ratio >= 0.975 && ratio <= 1.05);
#endif
    }


    bool improvement = false;

    long double best_prob = base_prob;
    bool best_change_is_move = false;
    uint best_move_j = MAX_UINT;
    uint best_move_aj = MAX_UINT;
    uint best_swap_j1 = MAX_UINT;
    uint best_swap_j2 = MAX_UINT;

    /**** scan neighboring alignments and keep track of the best one that is better 
     ****  than the current alignment  ****/

    //Math1D::Vector<AlignBaseType> hyp_alignment = alignment;

    /**** expansion moves ****/
    
    for (uint j=0; j < curJ; j++) {

      const uint s_idx = source[j];

      const uint aj = alignment[j];
      expansion_prob(j,aj) = 0.0;

      hyp_aligned_source_words[aj].erase(std::find(hyp_aligned_source_words[aj].begin(),hyp_aligned_source_words[aj].end(),j));

      const double old_dict_prob = (aj == 0) ? dict_[0][s_idx-1] : dict_[target[aj-1]][lookup(j,aj-1)];

      for (uint cand_aj = 0; cand_aj <= curI; cand_aj++) {

        if (aj == cand_aj) {
          expansion_prob(j,cand_aj) = 0.0;
          continue;
        }

	if (cand_aj > 0) {
	  if ((fertility[cand_aj]+1) > fertility_limit_) {
	    expansion_prob(j,cand_aj) = 0.0;
	    continue;
	  }
	}
	if (cand_aj == 0 && 2*fertility[0]+2 > curJ) { //better to check this before computing distortion probs
	  expansion_prob(j,cand_aj) = 0.0;
	  continue;
	}

        const double new_dict_prob = (cand_aj == 0) ? dict_[0][s_idx-1] : dict_[target[cand_aj-1]][lookup(j,cand_aj-1)];

        if (new_dict_prob < 1e-8)
          expansion_prob(j,cand_aj) = 0.0;
        else {
          //hyp_alignment[j] = cand_aj;
          hyp_aligned_source_words[cand_aj].push_back(j);
	  vec_sort(hyp_aligned_source_words[cand_aj]);

          long double leaving_prob = base_distortion_prob * old_dict_prob;
          long double incoming_prob = distortion_prob(source,target,hyp_aligned_source_words)
            * new_dict_prob;

          if (aj > 0) {
            uint tidx = target[aj-1];
            leaving_prob *= fertility_prob_[tidx][fertility[aj]];
            incoming_prob *= fertility_prob_[tidx][fertility[aj]-1];
          }
          else {
            
            //compute null-fert-model (null-fert decreases by 1)

            incoming_prob *= empty_word_decrease_const;
          }

          if (cand_aj > 0) {
            uint tidx = target[cand_aj-1];
            leaving_prob *= fertility_prob_[tidx][fertility[cand_aj]];
            incoming_prob *= fertility_prob_[tidx][fertility[cand_aj]+1]; 
          }
          else {
            if (curJ < 2*fertility[0]+2)
              incoming_prob = 0.0;
            else {
              
              //compute null-fert-model (zero-fert goes up by 1)
              
              incoming_prob *= empty_word_increase_const;
            }
          }

          long double incremental_cand_prob = base_prob * incoming_prob / leaving_prob;

          //DEBUG
          // long double cand_prob = alignment_prob(source,target,lookup,hyp_alignment);

          // long double ratio = incremental_cand_prob / cand_prob;

          // if (cand_prob > 1e-250) {
          //   if (! (ratio >= 0.99 && ratio <= 1.01)) {
          //     std::cerr << "j: " << j << ", aj: " << aj << ", cand_aj: " << cand_aj << std::endl;
          //     std::cerr << "incremental: " << incremental_cand_prob << ", standalone: " << cand_prob << std::endl; 
          //   }
          //   assert(ratio >= 0.99 && ratio <= 1.01);
          // }
          //END_DEBUG

          expansion_prob(j,cand_aj) = incremental_cand_prob;

          if (incremental_cand_prob > improvement_factor * best_prob) {
            improvement = true;
            best_change_is_move = true;
            best_prob = incremental_cand_prob;
            best_move_j = j;
            best_move_aj = cand_aj;
          }

          //restore for the next iteration
          //hyp_alignment[j] = aj; 
          hyp_aligned_source_words[cand_aj] = aligned_source_words[cand_aj];
        }
      }

      hyp_aligned_source_words[aj] = aligned_source_words[aj];
    }

    /**** swap moves ****/
    for (uint j1=0; j1 < curJ; j1++) {

      const uint aj1 = alignment[j1];
      const uint s_j1 = source[j1];

      for (uint j2 = j1+1; j2 < curJ; j2++) {

        const uint aj2 = alignment[j2];
        const uint s_j2 = source[j2];

        if (aj1 == aj2) {
          //we do not want to count the same alignment twice
          swap_prob(j1,j2) = 0.0;
        }
        else {

          //std::swap(hyp_alignment[j1],hyp_alignment[j2]);
          
          for (uint k=0; k < hyp_aligned_source_words[aj2].size(); k++) {
            if (hyp_aligned_source_words[aj2][k] == j2) {
              hyp_aligned_source_words[aj2][k] = j1;
              break;
            }
          }
          for (uint k=0; k < hyp_aligned_source_words[aj1].size(); k++) {
            if (hyp_aligned_source_words[aj1][k] == j1) {
              hyp_aligned_source_words[aj1][k] = j2;
              break;
            }
          }

	  vec_sort(hyp_aligned_source_words[aj1]);
	  vec_sort(hyp_aligned_source_words[aj2]);
          
          long double incremental_prob = base_prob / base_distortion_prob * 
            distortion_prob(source,target,hyp_aligned_source_words);

          if (aj1 != 0) {
            const uint t_idx = target[aj1-1];
            incremental_prob *= dict_[t_idx][lookup(j2,aj1-1)] 
              / dict_[t_idx][lookup(j1,aj1-1)] ;
          }
          else
            incremental_prob *= dict_[0][s_j2-1] / dict_[0][s_j1-1];

          if (aj2 != 0) {
            const uint t_idx = target[aj2-1];
            incremental_prob *= dict_[t_idx][lookup(j1,aj2-1)] 
              / dict_[t_idx][lookup(j2,aj2-1)] ;
          }
          else {
            incremental_prob *= dict_[0][s_j1-1] / dict_[0][s_j2-1];
          }

          //DEBUG
          // long double cand_prob = alignment_prob(source,target,lookup,hyp_alignment);          

          // if (cand_prob > 1e-250) {

          //   double ratio = cand_prob / incremental_prob;
          //   assert(ratio > 0.99 && ratio < 1.01);
          // }
          //END_DEBUG
	  
          
          swap_prob(j1,j2) = incremental_prob;

          if (incremental_prob > improvement_factor * best_prob) {
            improvement = true;
            best_change_is_move = false;
            best_prob = incremental_prob;
            best_swap_j1 = j1;
            best_swap_j2 = j2;
          }

          //restore for the next iteration
          //std::swap(hyp_alignment[j1],hyp_alignment[j2]);
          hyp_aligned_source_words[aj1] = aligned_source_words[aj1];
          hyp_aligned_source_words[aj2] = aligned_source_words[aj2];
        }
      }
    }


    /**** update to best alignment ****/

    if (!improvement || count_iter > 150)
      break;

    //update alignment
    if (best_change_is_move) {
      uint cur_aj = alignment[best_move_j];
      assert(cur_aj != best_move_aj);

      //std::cerr << "moving source pos" << best_move_j << " from " << cur_aj << " to " << best_move_aj << std::endl;

      alignment[best_move_j] = best_move_aj;
      fertility[cur_aj]--;
      fertility[best_move_aj]++;

      aligned_source_words[cur_aj].erase(std::find(aligned_source_words[cur_aj].begin(),aligned_source_words[cur_aj].end(),best_move_j));
      aligned_source_words[best_move_aj].push_back(best_move_j);
      vec_sort(aligned_source_words[best_move_aj]);

      hyp_aligned_source_words[cur_aj] = aligned_source_words[cur_aj];
      hyp_aligned_source_words[best_move_aj] = aligned_source_words[best_move_aj];
    }
    else {
      //std::cerr << "swapping: j1=" << best_swap_j1 << std::endl;
      //std::cerr << "swapping: j2=" << best_swap_j2 << std::endl;

      uint cur_aj1 = alignment[best_swap_j1];
      uint cur_aj2 = alignment[best_swap_j2];

      assert(cur_aj1 != cur_aj2);
      
      alignment[best_swap_j1] = cur_aj2;
      alignment[best_swap_j2] = cur_aj1;

      for (uint k=0; k < aligned_source_words[cur_aj2].size(); k++) {
	if (aligned_source_words[cur_aj2][k] == best_swap_j2) {
	  aligned_source_words[cur_aj2][k] = best_swap_j1;
          break;
        }
      }
      for (uint k=0; k < aligned_source_words[cur_aj1].size(); k++) {
	if (aligned_source_words[cur_aj1][k] == best_swap_j1) {
	  aligned_source_words[cur_aj1][k] = best_swap_j2;
          break;
        }
      }

      vec_sort(aligned_source_words[cur_aj1]);
      vec_sort(aligned_source_words[cur_aj2]);

      hyp_aligned_source_words[cur_aj1] = aligned_source_words[cur_aj1];
      hyp_aligned_source_words[cur_aj2] = aligned_source_words[cur_aj2];
    }

    base_prob = best_prob;
    base_distortion_prob = distortion_prob(source,target,aligned_source_words);
  }

  //symmetrize swap_prob
  for (uint j1=0; j1 < curJ; j1++) {

    swap_prob(j1,j1) = 0.0;
    
    for (uint j2 = j1+1; j2 < curJ; j2++) {
      
      swap_prob(j2,j1) = swap_prob(j1,j2);
    }
  }

  return base_prob;
}

double IBM5Trainer::inter_distortion_m_step_energy(uint sclass, const Storage1D<Math3D::Tensor<double> >& count, 
						   const Math2D::Matrix<double>& param) {

  double energy = 0.0;

  for (uint J=1; J < count.size(); J++) {
    
    for (uint prev_pos = 0; prev_pos < count[J].yDim(); prev_pos++) {

      double param_sum = 0.0;
      double count_sum = 0.0;
      
      for (uint j=0; j < J; j++) {
	
	double cur_weight = count[J](j,prev_pos,sclass);
	double cur_param = param(displacement_offset_+j-prev_pos,sclass);

	assert(cur_param >= 1e-15);
	
	count_sum += cur_weight;
	param_sum += cur_param;
	
	energy -= cur_weight * std::log(cur_param);
      }
      
      energy += count_sum * std::log(param_sum);
    }
  }

  return energy;
}

void IBM5Trainer::inter_distortion_m_step(uint sclass, const Storage1D<Math3D::Tensor<double> >& count) {

  for (uint k=0; k < inter_distortion_param_.xDim(); k++)
    inter_distortion_param_(k,sclass) = std::max(1e-15,inter_distortion_param_(k,sclass));

  Math1D::Vector<double> gradient(inter_distortion_param_.xDim());
  Math1D::Vector<double> new_param(inter_distortion_param_.xDim());
  Math2D::Matrix<double> hyp_param = inter_distortion_param_;

  double energy = inter_distortion_m_step_energy(sclass,count,inter_distortion_param_);

  { //check if normalized expectations give a better starting point


    for (uint k=0; k < hyp_param.xDim(); k++)
      hyp_param(k,sclass) = 0.0;

    for (uint J=0; J < count.size(); J++) {

      for (uint prev_pos = 0; prev_pos < count[J].yDim(); prev_pos++) {

	for (uint j=0; j < J; j++) {
	  
	  double cur_weight = count[J](j,prev_pos,sclass);
	  hyp_param(displacement_offset_+j-prev_pos,sclass) += cur_weight;
	}
      }
    }

    double sum = 0.0;
    for (uint j=0; j < inter_distortion_param_.xDim(); j++)
      sum += hyp_param(j,sclass);
    
    assert(sum > 1e-305);
    
    for (uint j=0; j < inter_distortion_param_.xDim(); j++)
      hyp_param(j,sclass) = std::max(1e-15, hyp_param(j,sclass) / sum);
    
    double hyp_energy = inter_distortion_m_step_energy(sclass, count, hyp_param);

    if (hyp_energy < energy) {

      for (uint j=0; j < inter_distortion_param_.xDim(); j++)
	inter_distortion_param_(j,sclass) = hyp_param(j,sclass);

      energy = hyp_energy;
    }
  }
  
  if (nSourceClasses_ <= 4)
    std::cerr << "start energy: " << energy << std::endl;
  
  double alpha = 0.1;
  double line_reduction_factor = 0.35;

  for (uint iter=1; iter <= 50; iter++) {

    if ((iter % 5) == 0) {

      if (nSourceClasses_ <= 4)
	std::cerr << "iteration #" << iter << ", energy: " << energy << std::endl;
    } 
    
    gradient.set_constant(0.0);
    
    /*** 1. calculate gradient ***/
    
    for (uint J=1; J < count.size(); J++) {
      
      for (uint prev_pos = 0; prev_pos < count[J].yDim(); prev_pos++) {
	
	double param_sum = 0.0;
	double count_sum = 0.0;
	
	for (uint j=0; j < J; j++) {
	  
	  const uint pos = displacement_offset_+j-prev_pos;
	  
	  const double cur_weight = count[J](j,prev_pos,sclass);
	  const double cur_param = inter_distortion_param_(pos,sclass);

	  assert(cur_param >= 1e-15);

	  count_sum += cur_weight;
	  param_sum += cur_param;
	  
	  gradient[pos] -= cur_weight / cur_param;
	}
	
	const double weight = count_sum / param_sum;
	
	for (uint j=0; j < J; j++) {
	  
	  const uint pos = displacement_offset_+j-prev_pos;
	  
	  gradient[pos] += weight;
	}
      }	
    }

    /*** 2. go in neg. gradient direction ***/
    for (uint j=0; j < gradient.size(); j++)
      new_param[j] = inter_distortion_param_(j,sclass) - alpha * gradient[j];
    
    /*** 3. reproject ***/
    projection_on_simplex(new_param.direct_access(), new_param.size()); 

    for (uint j=0; j < gradient.size(); j++)
      new_param[j] = std::max(1e-15,new_param[j]);
    
    /*** 4. find appropriate step size ***/

    double best_lambda = 1.0;
    double lambda = 1.0;

    double best_energy = 1e300;

    uint nIter = 0;

    bool decreasing = false;

    while (decreasing || best_energy > energy) {

      nIter++;

      lambda *= line_reduction_factor;
      double neg_lambda = 1.0 - lambda;

      for (uint j=0; j < inter_distortion_param_.xDim(); j++)   
        hyp_param(j,sclass) = std::max(lambda * new_param[j]
				       + neg_lambda * inter_distortion_param_(j,sclass), 1e-15);

      double hyp_energy = inter_distortion_m_step_energy(sclass, count, hyp_param);

      if (hyp_energy < best_energy) {

        best_energy = hyp_energy;
        best_lambda = lambda;
        decreasing = true;
      }
      else
        decreasing = false;

      if (nIter > 5 && best_energy < 0.975 * energy)
        break;

      if (nIter > 15 && lambda < 1e-12)
	break;
    }

    if (best_energy >= energy) {
      std::cerr << "CUTOFF after " << iter << " iterations" << std::endl;
      break;
    }

    if (nIter > 6)
      line_reduction_factor *= 0.9;

    double neg_best_lambda = 1.0 - best_lambda;

    for (uint j=0; j < inter_distortion_param_.xDim(); j++)   
      inter_distortion_param_(j,sclass) = std::max(1e-15, best_lambda * new_param[j]
						   + neg_best_lambda * inter_distortion_param_(j,sclass));

    energy = best_energy;
  }
}

double IBM5Trainer::intra_distortion_m_step_energy(uint sclass, const Storage1D<Math2D::Matrix<double> >& count, 
						   const Math2D::Matrix<double>& param) {

  double energy = 0.0;

  for (uint J=1; J < count.size(); J++) {

    double count_sum = 0.0;
    double param_sum = 0.0;

    const Math2D::Matrix<double>& cur_count = count[J];

    for (uint j=0; j < J; j++) {
    
      double cur_weight = cur_count(j,sclass);
      double cur_param = std::max(1e-15,param(j,sclass));

      count_sum += cur_weight;
      param_sum += cur_param;

      energy -= cur_weight * std::log(cur_param);
    }

    energy += count_sum * std::log(param_sum);
  }

  return energy;
}

void IBM5Trainer::intra_distortion_m_step(uint sclass, const Storage1D<Math2D::Matrix<double> >& count) {

  Math1D::Vector<double> gradient(intra_distortion_param_.xDim());
  Math1D::Vector<double> new_param(intra_distortion_param_.xDim());
  Math2D::Matrix<double> hyp_param(intra_distortion_param_.xDim(),intra_distortion_param_.yDim());

  for (uint j=0; j < gradient.size(); j++)
    intra_distortion_param_(j,sclass) = std::max(1e-15,intra_distortion_param_(j,sclass));

  double energy = intra_distortion_m_step_energy(sclass,count,intra_distortion_param_);


  { //check if normalized expectations give a better starting point


    for (uint k=0; k < hyp_param.xDim(); k++)
      hyp_param(k,sclass) = 0.0;

    for (uint J=0; J < count.size(); J++) {


      for (uint j=0; j < J; j++) {
	
	hyp_param(j,sclass) += count[J](j,sclass);
      }
    }

    double sum = 0.0;
    for (uint k=0; k < hyp_param.xDim(); k++)
      sum += hyp_param(k,sclass);

    if (sum > 1e-305) {

      for (uint k=0; k < hyp_param.xDim(); k++)
	hyp_param(k,sclass) = std::max(1e-15,hyp_param(k,sclass) / sum);
    
      double hyp_energy = intra_distortion_m_step_energy(sclass,count,hyp_param);

      if (hyp_energy < energy) {

	for (uint j=0; j < hyp_param.xDim(); j++)
	  intra_distortion_param_(j,sclass) = hyp_param(j,sclass);

	energy = hyp_energy;
      }
    }
  }



  if (nSourceClasses_ <= 4)
    std::cerr << "start energy: " << energy << std::endl;

  double alpha = 0.1;
  double line_reduction_factor = 0.35;
  
  for (uint iter = 1; iter <= 50; iter++) {

    gradient.set_constant(0.0);

    if ((iter % 5) == 0) {

      if (nSourceClasses_ <= 4)
	std::cerr << "iteration #" << iter << ", energy: " << energy << std::endl;
    } 
    
    /*** 1. calculate gradient ***/

    for (uint J=1; J < count.size(); J++) {

      double count_sum = 0.0;
      double param_sum = 0.0;
      
      const Math2D::Matrix<double>& cur_count = count[J];

      for (uint j=0; j < J; j++) {
    
	double cur_weight = cur_count(j,sclass);
	double cur_param = std::max(1e-15,intra_distortion_param_(j,sclass));
      
	count_sum += cur_weight;
	param_sum += cur_param;
	
	gradient[j] -= cur_weight / cur_param;
      }

      const double weight = count_sum / param_sum;
      
      for (uint j=0; j < J; j++) 
	gradient[j] += weight;
    }

    /*** 2. go in neg. gradient direction ***/
    for (uint j=0; j < gradient.size(); j++)
      new_param[j] = intra_distortion_param_(j,sclass) - alpha * gradient[j];

    /*** 3. reproject ***/
    projection_on_simplex(new_param.direct_access(), new_param.size()); 

    /*** 4. find appropriate step size ***/
    double best_lambda = 1.0;
    double lambda = 1.0;

    double best_energy = 1e300;

    uint nIter = 0;

    bool decreasing = false;

    while (decreasing || best_energy > energy) {

      nIter++;

      lambda *= line_reduction_factor;
      double neg_lambda = 1.0 - lambda;

      for (uint j=0; j < intra_distortion_param_.xDim(); j++)   
        hyp_param(j,sclass) = std::max(1e-15, lambda * new_param[j]
				       + neg_lambda * intra_distortion_param_(j,sclass));

      double hyp_energy = intra_distortion_m_step_energy(sclass, count, hyp_param);

      if (hyp_energy < best_energy) {

        best_energy = hyp_energy;
        best_lambda = lambda;
        decreasing = true;
      }
      else
        decreasing = false;

      if (nIter > 5 && best_energy < 0.975 * energy)
        break;

      if (nIter > 15 && lambda < 1e-12)
	break;
    }

    if (best_energy >= energy) {
      std::cerr << "CUTOFF after " << iter << " iterations" << std::endl;
      break;
    }

    if (nIter > 6)
      line_reduction_factor *= 0.9;

    double neg_best_lambda = 1.0 - best_lambda;

    for (uint j=0; j < intra_distortion_param_.xDim(); j++)   
      intra_distortion_param_(j,sclass) = std::max(1e-15, best_lambda * new_param[j]
						   + neg_best_lambda * intra_distortion_param_(j,sclass));

    energy = best_energy;

  }
}

void IBM5Trainer::train_unconstrained(uint nIter, FertilityModelTrainer* fert_trainer, HmmWrapper* wrapper) {

  std::cerr << "starting IBM-5 training without constraints";
  if (fert_trainer != 0)
    std::cerr << " (init from " << fert_trainer->model_name() <<  ") "; 
  else if (wrapper != 0)
    std::cerr << " (init from HMM) ";
  std::cerr << std::endl;

  double max_perplexity = 0.0;
  double approx_sum_perplexity = 0.0;

  Storage1D<Math1D::Vector<AlignBaseType> > initial_alignment;
  if (hillclimb_mode_ == HillclimbingRestart) 
    initial_alignment = best_known_alignment_; //CAUTION: we will save here the alignment from the IBM-4, NOT from the HMM

  SingleLookupTable aux_lookup;

  double dict_weight_sum = 0.0;
  for (uint i=0; i < nTargetWords_; i++) {
    dict_weight_sum += fabs(prior_weight_[i].sum());
  }

  const uint nTargetWords = dict_.size();

  NamedStorage1D<Math1D::Vector<double> > fwcount(nTargetWords,MAKENAME(fwcount));
  NamedStorage1D<Math1D::Vector<double> > ffert_count(nTargetWords,MAKENAME(ffert_count));

  for (uint i=0; i < nTargetWords; i++) {
    fwcount[i].resize(dict_[i].size());
    ffert_count[i].resize_dirty(fertility_prob_[i].size());
  }
 
  double fzero_count;
  double fnonzero_count;

  Storage1D<Math3D::Tensor<double> > inter_distortion_count = inter_distortion_prob_;

  Storage1D<Math2D::Matrix<double> > intra_distortion_count = intra_distortion_prob_;

  Storage1D<Math1D::Vector<double> > sentence_start_count = sentence_start_prob_;

  uint iter;
  for (iter=1+iter_offs_; iter <= nIter+iter_offs_; iter++) {

    std::cerr << "******* IBM-5 EM-iteration " << iter << std::endl;

    uint sum_iter = 0;

    /*** clear counts ***/
    for (uint J=1; J < inter_distortion_count.size(); J++)
      inter_distortion_count[J].set_constant(0.0);

    for (uint J=1; J < intra_distortion_count.size(); J++) {
      intra_distortion_count[J].set_constant(0.0);
      sentence_start_count[J].set_constant(0.0);
    }

    fzero_count = 0.0;
    fnonzero_count = 0.0;

    for (uint i=0; i < nTargetWords; i++) {
      fwcount[i].set_constant(0.0);
      ffert_count[i].set_constant(0.0);
    }

    max_perplexity = 0.0;
    approx_sum_perplexity = 0.0;
 
    double hillclimbtime = 0.0;
    double countcollecttime = 0.0;
   
    for (size_t s=0; s < source_sentence_.size(); s++) {

      if ((s% 10000) == 0)
        std::cerr << "sentence pair #" << s << std::endl;
      
      const Storage1D<uint>& cur_source = source_sentence_[s];
      const Storage1D<uint>& cur_target = target_sentence_[s];
      const SingleLookupTable& cur_lookup = get_wordlookup(cur_source,cur_target,wcooc_,
                                                           nSourceWords_,slookup_[s],aux_lookup);
      
      const uint curI = cur_target.size();
      const uint curJ = cur_source.size();

      Math1D::NamedVector<uint> fertility(curI+1,0,MAKENAME(fertility));

      Math2D::NamedMatrix<long double> swap_move_prob(curJ,curJ,MAKENAME(swap_move_prob));
      Math2D::NamedMatrix<long double> expansion_move_prob(curJ,curI+1,MAKENAME(expansion_move_prob));

      std::clock_t tHillclimbStart, tHillclimbEnd;
      tHillclimbStart = std::clock();

      long double best_prob = 0.0;

      if (hillclimb_mode_ == HillclimbingRestart) 
	best_known_alignment_[s] = initial_alignment[s];

      if (fert_trainer != 0 && iter == 1) {
	best_prob = fert_trainer->update_alignment_by_hillclimbing(cur_source,cur_target,cur_lookup,sum_iter,fertility,
							   expansion_move_prob,swap_move_prob,best_known_alignment_[s]);
      }
      else if (wrapper != 0 && iter == 1) {
	best_prob = simulate_hmm_hillclimbing(cur_source, cur_target, cur_lookup, *wrapper,
					      fertility, expansion_move_prob, swap_move_prob, best_known_alignment_[s]);
      }
      else {
	best_prob = update_alignment_by_hillclimbing(cur_source,cur_target,cur_lookup,sum_iter,fertility,
						     expansion_move_prob,swap_move_prob,best_known_alignment_[s]);
      }
      max_perplexity -= std::log(best_prob);

      tHillclimbEnd = std::clock();

      hillclimbtime += diff_seconds(tHillclimbEnd,tHillclimbStart);

      const long double expansion_prob = expansion_move_prob.sum();
      const long double swap_prob =  swap_mass(swap_move_prob); 

      const long double sentence_prob = best_prob + expansion_prob +  swap_prob;

      assert(!isnan(sentence_prob));

      approx_sum_perplexity -= std::log(sentence_prob);
      
      const long double inv_sentence_prob = 1.0 / sentence_prob;

      /****** collect counts ******/

      /**** update empty word counts *****/

      update_zero_counts(best_known_alignment_[s], fertility,
			 expansion_move_prob, swap_prob, best_prob,
			 sentence_prob, inv_sentence_prob,
			 fzero_count, fnonzero_count);

      /**** update fertility counts *****/
      update_fertility_counts(cur_target, best_known_alignment_[s], fertility,
			      expansion_move_prob, sentence_prob, inv_sentence_prob, ffert_count);	

      /**** update dictionary counts *****/
      update_dict_counts(cur_source, cur_target, cur_lookup, best_known_alignment_[s],
			 expansion_move_prob, swap_move_prob, sentence_prob, inv_sentence_prob,fwcount);

      std::clock_t tCountCollectStart, tCountCollectEnd;
      tCountCollectStart = std::clock();

      /**** update distortion counts *****/

      //1. Viterbi alignment
      NamedStorage1D<std::vector<ushort> > aligned_source_words(curI+1,MAKENAME(aligned_source_words));
      for (uint j=0; j < curJ; j++)
	aligned_source_words[best_known_alignment_[s][j]].push_back(j);

      {
	const double increment = best_prob * inv_sentence_prob;

	uint prev_center = MAX_UINT;

	Storage1D<bool> fixed(curJ,false);

	uint nOpen = curJ;

	for (uint i=1; i <= curI; i++) {
	  
	  if (fertility[i] > 0) {
	   
	    const std::vector<ushort>& cur_aligned_source_words = aligned_source_words[i];
	    assert(cur_aligned_source_words.size() == fertility[i]);

	    //a) head of the cept
	    const uint first_j = cur_aligned_source_words[0];

	    if (prev_center != MAX_UINT) { // currently not estimating a start probability
	      
	      const uint sclass = source_class_[cur_source[first_j]];

	      const uint nAvailable = nOpen - (cur_aligned_source_words.size()-1);

	      Math3D::Tensor<double>& cur_inter_distortion_count = inter_distortion_count[nAvailable];

	      uint pos_first_j = MAX_UINT;
	      uint pos_prev_center = MAX_UINT;
	      
	      uint nCurOpen = 0;
	      for (uint j = 0; j <= std::max(first_j,prev_center); j++) {
		
		if (j == first_j)
		  pos_first_j = nCurOpen;
		
		if (!fixed[j])
		  nCurOpen++;
		
		if (j == prev_center)
		  pos_prev_center = nCurOpen;
	      }

	      //DEBUG
	      if (pos_prev_center >= cur_inter_distortion_count.yDim()) {

		std::cerr << "J= " << curJ << ", prev_center=" << prev_center << ", pos_prev_center = " << pos_prev_center << std::endl;
		std::cerr << "fixed: " << fixed << std::endl;
	      }
	      //END_DEBUG

	      assert(pos_prev_center < cur_inter_distortion_count.yDim());

	      
	      cur_inter_distortion_count(pos_first_j,pos_prev_center,sclass) += increment;
	    }
	    else if (use_sentence_start_prob_) {
	      sentence_start_count[curJ][first_j] += increment;
	    }

	    fixed[first_j] = true;
	    nOpen--;

	    //b) body of the cept
	    for (uint k=1; k < cur_aligned_source_words.size(); k++) {

	      const uint cur_j = cur_aligned_source_words[k];

	      const uint sclass = source_class_[cur_source[cur_j]];

	      uint pos = MAX_UINT;

	      uint nAvailable = 0;
	      for (uint j = cur_aligned_source_words[k-1]+1; j < curJ; j++) {
		
		if (j == cur_j)
		  pos = nAvailable;
		
		if (!fixed[j])
		  nAvailable++;
	      }

	      nAvailable -= cur_aligned_source_words.size() - 1 - k;
	      
	      intra_distortion_count[nAvailable](pos,sclass) += increment;

	      fixed[cur_j] = true;
	      nOpen--;
	    }

	    //c) calculate the center of the cept
	    switch (cept_start_mode_) {
	    case IBM4CENTER : {
	      
	      //compute the center of this cept and store the result in prev_cept_center
	      double sum = 0.0;
	      for (uint k=0; k < cur_aligned_source_words.size(); k++) {
		sum += cur_aligned_source_words[k];
	      }
	      
	      prev_center = (int) round(sum / cur_aligned_source_words.size());
	      break;
	    }
	    case IBM4FIRST:
	      prev_center = first_j;
	      break;
	    case IBM4LAST:
	      prev_center = cur_aligned_source_words.back();
	      break;
	    case IBM4UNIFORM:
	      break;
	    default:
	      assert(false);
	    }

	  }
	}
      }

      NamedStorage1D<std::vector<ushort> > hyp_aligned_source_words(MAKENAME(hyp_aligned_source_words));
      hyp_aligned_source_words = aligned_source_words;

      //2. expansion moves
      for (uint j=0; j < curJ; j++) {
	
	uint cur_aj = best_known_alignment_[s][j];

	hyp_aligned_source_words[cur_aj].erase(std::find(hyp_aligned_source_words[cur_aj].begin(),
							 hyp_aligned_source_words[cur_aj].end(),j));

	for (uint aj=0; aj <= curI; aj++) {

	  if (expansion_move_prob(j,aj) > best_prob * 1e-11) {

	    const double increment = expansion_move_prob(j,aj) * inv_sentence_prob;

	    hyp_aligned_source_words[aj].push_back(j);
	    vec_sort(hyp_aligned_source_words[aj]);

	    uint prev_center = MAX_UINT;

	    Storage1D<bool> fixed(curJ,false);

	    uint nOpen = curJ;
	    
	    for (uint i=1; i <= curI; i++) {
	    
	      const std::vector<ushort>& cur_aligned_source_words = hyp_aligned_source_words[i];

	      if (cur_aligned_source_words.size() > 0) {

		//a) head of the cept
		const uint first_j = cur_aligned_source_words[0];

		if (prev_center != MAX_UINT) { // currently not estimating a start probability
	      
		  const uint sclass = source_class_[cur_source[first_j]];
		  
		  const uint nAvailable = nOpen - (cur_aligned_source_words.size()-1);
	      
		  Math3D::Tensor<double>& cur_inter_distortion_count = inter_distortion_count[nAvailable];

		  uint pos_first_j = MAX_UINT;
		  uint pos_prev_center = MAX_UINT;
		  
		  uint nCurOpen = 0;
		  for (uint j = 0; j <= std::max(first_j,prev_center); j++) {
		    
		    if (j == first_j)
		      pos_first_j = nCurOpen;
		    
		    if (!fixed[j])
		      nCurOpen++;
		    
		    if (j == prev_center)
		      pos_prev_center = nCurOpen;
		  }

		  assert(pos_prev_center < cur_inter_distortion_count.yDim());
	      
		  cur_inter_distortion_count(pos_first_j,pos_prev_center,sclass) += increment;
		}
		else if (use_sentence_start_prob_) {
		  sentence_start_count[curJ][first_j] += increment;
		}

		fixed[first_j] = true;
		nOpen--;

		//b) body of the cept
		for (uint k=1; k < cur_aligned_source_words.size(); k++) {
		  
		  const uint cur_j = cur_aligned_source_words[k];
		  
		  const uint sclass = source_class_[cur_source[cur_j]];
		  
		  uint pos = MAX_UINT;
		  
		  uint nAvailable = 0;
		  for (uint j = cur_aligned_source_words[k-1]+1; j < curJ; j++) {
		    
		    if (j == cur_j)
		      pos = nAvailable;
		    
		    if (!fixed[j])
		      nAvailable++;
		  }
		  
		  nAvailable -= cur_aligned_source_words.size() - 1 - k;
	      
		  intra_distortion_count[nAvailable](pos,sclass) += increment;
		  
		  fixed[cur_j] = true;
		  nOpen--;
		}


		//c) calculate the center of the cept
		switch (cept_start_mode_) {
		case IBM4CENTER : {
		  
		  //compute the center of this cept and store the result in prev_cept_center
		  double sum = 0.0;
		  for (uint k=0; k < cur_aligned_source_words.size(); k++) {
		    sum += cur_aligned_source_words[k];
		  }
		  
		  prev_center = (int) round(sum / cur_aligned_source_words.size());
		  break;
		}
		case IBM4FIRST:
		  prev_center = first_j;
		  break;
		case IBM4LAST:
		  prev_center = cur_aligned_source_words.back();
		  break;
		case IBM4UNIFORM:
		  break;
		default:
		  assert(false);
		}

	      }
	    }
	  
	    //restore hyp_aligned_source_words
	    hyp_aligned_source_words[aj] = aligned_source_words[aj];
	  }
	}
	
	//restore hyp_aligned_source_words
	hyp_aligned_source_words[cur_aj] = aligned_source_words[cur_aj];
      }

      //3. swap moves
      for (uint j1 = 0; j1 < curJ-1; j1++) {

	//std::cerr << "j1: " << j1 << std::endl;

	const uint aj1 = best_known_alignment_[s][j1];

	for (uint j2 = j1+1; j2 < curJ; j2++) {

	  if (swap_move_prob(j1,j2) > best_prob * 1e-11) {

	    const uint aj2 = best_known_alignment_[s][j2];
	    
	    for (uint k=0; k < hyp_aligned_source_words[aj2].size(); k++) {
	      if (hyp_aligned_source_words[aj2][k] == j2) {
		hyp_aligned_source_words[aj2][k] = j1;
		break;
	      }
	    }
	    for (uint k=0; k < hyp_aligned_source_words[aj1].size(); k++) {
	      if (hyp_aligned_source_words[aj1][k] == j1) {
		hyp_aligned_source_words[aj1][k] = j2;
		break;
	      }
	    }
	    
	    vec_sort(hyp_aligned_source_words[aj1]);
	    vec_sort(hyp_aligned_source_words[aj2]);
	    
	    const double increment = swap_move_prob(j1,j2) * inv_sentence_prob;

	    uint prev_center = MAX_UINT;

	    Storage1D<bool> fixed(curJ,false);
	    
	    uint nOpen = curJ;
	    
	    for (uint i=1; i <= curI; i++) {

	      const std::vector<ushort>& cur_aligned_source_words = hyp_aligned_source_words[i];

	      if (cur_aligned_source_words.size() > 0) {
		
		//a) head of the cept
		const uint first_j = cur_aligned_source_words[0];

		if (prev_center != MAX_UINT) { // currently not estimating a start probability
	      
		  const uint sclass = source_class_[cur_source[first_j]];
		  
		  const uint nAvailable = nOpen - (cur_aligned_source_words.size()-1);

		  Math3D::Tensor<double>& cur_inter_distortion_count = inter_distortion_count[nAvailable];
		  
		  uint pos_first_j = MAX_UINT;
		  uint pos_prev_center = MAX_UINT;
		  
		  uint nCurOpen = 0;
		  for (uint j = 0; j <= std::max(first_j,prev_center); j++) {
		    
		    if (j == first_j)
		      pos_first_j = nCurOpen;
		    
		    if (!fixed[j])
		      nCurOpen++;
		
		    if (j == prev_center)
		      pos_prev_center = nCurOpen;
		  }

		  //DEBUG
		  if (pos_prev_center >= cur_inter_distortion_count.yDim()) {
		    
		    std::cerr << "J= " << curJ << ", prev_center=" << prev_center << ", pos_prev_center = " << pos_prev_center << std::endl;
		    std::cerr << "fixed: " << fixed << std::endl;
		  }
		  //END_DEBUG

		  assert(pos_prev_center < cur_inter_distortion_count.yDim());

		  
		  cur_inter_distortion_count(pos_first_j,pos_prev_center,sclass) += increment;
		}
		else if (use_sentence_start_prob_) {
		  sentence_start_count[curJ][first_j] += increment;
		}

		fixed[first_j] = true;
		nOpen--;

		//b) body of the cept
		for (uint k=1; k < cur_aligned_source_words.size(); k++) {
		  
		  const uint cur_j = cur_aligned_source_words[k];
		  
		  const uint sclass = source_class_[cur_source[cur_j]];
		  
		  uint pos = MAX_UINT;
		  
		  uint nAvailable = 0;
		  for (uint j = cur_aligned_source_words[k-1]+1; j < curJ; j++) {
		    
		    if (j == cur_j)
		      pos = nAvailable;
		    
		    if (!fixed[j])
		      nAvailable++;
		  }
		  
		  nAvailable -= cur_aligned_source_words.size() - 1 - k;
	      
		  intra_distortion_count[nAvailable](pos,sclass) += increment;
		  
		  fixed[cur_j] = true;
		  nOpen--;
		}


		//c) calculate the center of the cept
		switch (cept_start_mode_) {
		case IBM4CENTER : {
		  
		  //compute the center of this cept and store the result in prev_cept_center
		  double sum = 0.0;
		  for (uint k=0; k < cur_aligned_source_words.size(); k++) {
		    sum += cur_aligned_source_words[k];
		  }
		  
		  prev_center = (int) round(sum / cur_aligned_source_words.size());
		  break;
		}
		case IBM4FIRST:
		  prev_center = first_j;
		  break;
		case IBM4LAST:
		  prev_center = cur_aligned_source_words.back();
		  break;
		case IBM4UNIFORM:
		  break;
		default:
		  assert(false);
		}

	      }
	    }

	    //restore hyp_aligned_source_words
	    hyp_aligned_source_words[aj1] = aligned_source_words[aj1];
	    hyp_aligned_source_words[aj2] = aligned_source_words[aj2];
	  }
	}
      }


    } //loop over sentences finished

    /***** update probability models from counts *******/

    //update p_zero_ and p_nonzero_
    if (!fix_p0_) {
      std::cerr << "zero counts: " << fzero_count << ", " << fnonzero_count << std::endl;
      double fsum = fzero_count + fnonzero_count;
      p_zero_ = fzero_count / fsum;
      p_nonzero_ = fnonzero_count / fsum;
    }

    std::cerr << "new p_zero: " << p_zero_ << std::endl;

    assert(!isnan(p_zero_));
    assert(!isnan(p_nonzero_));

    //DEBUG
    uint nZeroAlignments = 0;
    uint nAlignments = 0;
    for (size_t s=0; s < source_sentence_.size(); s++) {

      nAlignments += source_sentence_[s].size();

      for (uint j=0; j < source_sentence_[s].size(); j++) {
        if (best_known_alignment_[s][j] == 0)
          nZeroAlignments++;
      }
    }
    std::cerr << "percentage of zero-aligned words: " 
              << (((double) nZeroAlignments) / ((double) nAlignments)) << std::endl;
    //END_DEBUG

    //update dictionary
    update_dict_from_counts(fwcount, prior_weight_, dict_weight_sum, iter, 
			    smoothed_l0_, l0_beta_, 45, dict_, 1e-6);

    //update fertility probabilities
    update_fertility_prob(ffert_count,1e-8);

    //update inter distortion probabilities
    if (nonpar_distortion_) {

      for (uint J=1; J < inter_distortion_count.size(); J++) {

	for (uint y=0; y < inter_distortion_count[J].yDim(); y++) {
	  for (uint z=0; z < inter_distortion_count[J].zDim(); z++) {
	    
	    double sum = 0.0;
	    for (uint j=0; j < inter_distortion_count[J].xDim(); j++)
	      sum += inter_distortion_count[J](j,y,z);
	    
	    if (sum > 1e-305) {
	      
	      for (uint j=0; j < inter_distortion_count[J].xDim(); j++)
		inter_distortion_prob_[J](j,y,z) = std::max(1e-8,inter_distortion_count[J](j,y,z) / sum);
	    }
	  }
	}
      }
    }
    else {
  
      for (uint s=0; s < inter_distortion_param_.yDim(); s++) {

	inter_distortion_m_step(s, inter_distortion_count);
      }

      par2nonpar_inter_distortion();
    }


    //update intra distortion probabilities
    if (nonpar_distortion_) {

      for (uint J=1; J < intra_distortion_prob_.size(); J++) {
	
	for (uint s=0; s < intra_distortion_prob_[J].yDim(); s++) {

	  double sum = 0.0;
	  for (uint j=0; j < J; j++)
	    sum += intra_distortion_count[J](j,s);
	  
	  if (sum > 1e-305) {
	    for (uint j=0; j < J; j++)
	      intra_distortion_prob_[J](j,s) = std::max(1e-8,intra_distortion_count[J](j,s) / sum);
	  }
	}
      }
    }
    else {

      Math2D::Matrix<double> hyp_param = intra_distortion_param_;
      
      // call m-steps
      for (uint s=0; s < intra_distortion_param_.yDim(); s++) {
	
	intra_distortion_m_step(s,intra_distortion_count);
      }

      par2nonpar_intra_distortion();
    }


    if (use_sentence_start_prob_) {
      start_prob_m_step(sentence_start_count, sentence_start_parameters_);
      par2nonpar_start_prob(sentence_start_parameters_,sentence_start_prob_);
    }

    
    double reg_term = 0.0;
    for (uint i=0; i < dict_.size(); i++)
      for (uint k=0; k < dict_[i].size(); k++) {
	if (smoothed_l0_)
	  reg_term += prior_weight_[i][k] * prob_penalty(dict_[i][k],l0_beta_);
	else
	  reg_term += prior_weight_[i][k] * dict_[i][k];
      }
    
    max_perplexity += reg_term;
    approx_sum_perplexity += reg_term;


    max_perplexity /= source_sentence_.size();
    approx_sum_perplexity /= source_sentence_.size();

    std::string transfer = ((fert_trainer != 0 || wrapper != 0) && iter == 1) ? " (transfer) " : ""; 

    std::cerr << "IBM-5 max-perplex-energy in between iterations #" << (iter-1) << " and " << iter << transfer << ": "
              << max_perplexity << std::endl;
    std::cerr << "IBM-5 approx-sum-perplex-energy in between iterations #" << (iter-1) << " and " << iter << transfer << ": "
              << approx_sum_perplexity << std::endl;
    
    if (possible_ref_alignments_.size() > 0) {
      
      std::cerr << "#### IBM-5-AER in between iterations #" << (iter-1) << " and " << iter << transfer 
		<< ": " << AER() << std::endl;
      std::cerr << "#### IBM-5-fmeasure in between iterations #" << (iter-1) << " and " << iter << transfer 
		<< ": " << f_measure() << std::endl;
      std::cerr << "#### IBM-5-DAE/S in between iterations #" << (iter-1) << " and " << iter << transfer << ": " 
                << DAE_S() << std::endl;
    }

    std::cerr << (((double) sum_iter) / source_sentence_.size()) << " average hillclimbing iterations per sentence pair" 
              << std::endl;     

  }

  if (nonpar_distortion_) {

    //we still update <code> inter_distortion_param_ </code>  and <code> intra_distortion_param_ </code>
    //so that we can use them when computing external alignments


    //a) inter
    inter_distortion_param_.set_constant(0.0);    

    for (uint s=0; s < inter_distortion_param_.yDim(); s++) {
      
      for (uint J=0; J < inter_distortion_count.size(); J++) {
	
	for (uint prev_pos = 0; prev_pos < inter_distortion_count[J].yDim(); prev_pos++) {
	  
	  for (uint j=0; j < J; j++) {
	  
	    double cur_weight = inter_distortion_count[J](j,prev_pos,s);
	    inter_distortion_param_(displacement_offset_+j-prev_pos,s) += cur_weight;
	  }
	}
      }
      
      double sum = 0.0;
      for (uint j=0; j < inter_distortion_param_.xDim(); j++)
	sum += inter_distortion_param_(j,s);
      
      if (sum > 1e-305) {
	
	for (uint j=0; j < inter_distortion_param_.xDim(); j++)
	  inter_distortion_param_(j,s) = std::max(1e-8,inter_distortion_param_(j,s)/sum);
      }
      else {
	for (uint j=0; j < inter_distortion_param_.xDim(); j++)
	  inter_distortion_param_(j,s) = 1.0 / inter_distortion_param_.xDim();
      }
    }      

    //b) intra
    intra_distortion_param_.set_constant(0.0);    

    for (uint s=0; s < intra_distortion_param_.yDim(); s++) {

      for (uint J=0; J < intra_distortion_count.size(); J++) {
      
	for (uint j=0; j < J; j++)
	  intra_distortion_param_(j,s) += intra_distortion_count[J](j,s);
      }

      double sum = 0.0;
      for (uint j=0; j < intra_distortion_param_.xDim(); j++)
	sum += intra_distortion_param_(j,s);

      if (sum > 1e-305) {

	for (uint j=0; j < intra_distortion_param_.xDim(); j++)
	  intra_distortion_param_(j,s) = std::max(1e-8,intra_distortion_param_(j,s) / sum);
      }
      else {
      
	for (uint j=0; j < intra_distortion_param_.xDim(); j++)
	  intra_distortion_param_(j,s) = 1.0 / intra_distortion_param_.xDim();
      }
    }
  }


  iter_offs_ = iter-1;
}

void IBM5Trainer::train_viterbi(uint nIter, FertilityModelTrainer* fert_trainer, HmmWrapper* wrapper) {

  const uint nSentences = source_sentence_.size();

  std::cerr << "starting IBM-5 training without constraints";
  if (fert_trainer != 0)
    std::cerr << " (init from " << fert_trainer->model_name() <<  ") "; 
  else if (wrapper != 0)
    std::cerr << " (init from HMM) ";
  std::cerr << std::endl;

  Storage1D<Math1D::Vector<AlignBaseType> > initial_alignment;
  if (hillclimb_mode_ == HillclimbingRestart) 
    initial_alignment = best_known_alignment_; //CAUTION: we will save here the alignment from the IBM-4, NOT from the HMM

  double max_perplexity = 0.0;

  SingleLookupTable aux_lookup;

  double dict_weight_sum = 0.0;
  for (uint i=0; i < nTargetWords_; i++) {
    dict_weight_sum += fabs(prior_weight_[i].sum());
  }

  const uint nTargetWords = dict_.size();

  NamedStorage1D<Math1D::Vector<double> > fwcount(nTargetWords,MAKENAME(fwcount));
  NamedStorage1D<Math1D::Vector<double> > ffert_count(nTargetWords,MAKENAME(ffert_count));

  for (uint i=0; i < nTargetWords; i++) {
    fwcount[i].resize(dict_[i].size());
    ffert_count[i].resize_dirty(fertility_prob_[i].size());
  }
 
  long double fzero_count;
  long double fnonzero_count;

  Storage1D<Math3D::Tensor<double> > inter_distortion_count = inter_distortion_prob_;

  Storage1D<Math2D::Matrix<double> > intra_distortion_count = intra_distortion_prob_;

  Storage1D<Math1D::Vector<double> > sentence_start_count = sentence_start_prob_;

  uint iter;
  for (iter=1+iter_offs_; iter <= nIter+iter_offs_; iter++) {

    std::cerr << "******* IBM-5 EM-iteration " << iter << std::endl;

    uint sum_iter = 0;

    /*** clear counts ***/
    for (uint J=1; J < inter_distortion_count.size(); J++)
      inter_distortion_count[J].set_constant(0.0);

    for (uint J=1; J < intra_distortion_count.size(); J++) {
      intra_distortion_count[J].set_constant(0.0);
      sentence_start_count[J].set_constant(0.0);
    }

    fzero_count = 0.0;
    fnonzero_count = 0.0;

    for (uint i=0; i < nTargetWords; i++) {
      fwcount[i].set_constant(0.0);
      ffert_count[i].set_constant(0.0);
    }

    max_perplexity = 0.0;
 
    double hillclimbtime = 0.0;
    double countcollecttime = 0.0;
   
    for (size_t s=0; s < nSentences; s++) {

      if ((s% 10000) == 0)
        std::cerr << "sentence pair #" << s << std::endl;
      
      const Storage1D<uint>& cur_source = source_sentence_[s];
      const Storage1D<uint>& cur_target = target_sentence_[s];
      const SingleLookupTable& cur_lookup = get_wordlookup(cur_source,cur_target,wcooc_,
                                                           nSourceWords_,slookup_[s],aux_lookup);
      
      const uint curI = cur_target.size();
      const uint curJ = cur_source.size();

      Math1D::NamedVector<uint> fertility(curI+1,0,MAKENAME(fertility));

      Math2D::NamedMatrix<long double> swap_move_prob(curJ,curJ,MAKENAME(swap_move_prob));
      Math2D::NamedMatrix<long double> expansion_move_prob(curJ,curI+1,MAKENAME(expansion_move_prob));

      std::clock_t tHillclimbStart, tHillclimbEnd;
      tHillclimbStart = std::clock();

      long double best_prob = 0.0;

      if (hillclimb_mode_ == HillclimbingRestart) 
	best_known_alignment_[s] = initial_alignment[s];

      if (fert_trainer != 0 && iter == 1) {
	//std::cerr << "calling IBM-4 hillclimbing" << std::endl;
	best_prob = fert_trainer->update_alignment_by_hillclimbing(cur_source,cur_target,cur_lookup,sum_iter,fertility,
								   expansion_move_prob,swap_move_prob,best_known_alignment_[s]);
      }
      else if (wrapper != 0 && iter == 1) {
	//we can skip calling hillclimbing here - we don't look at the neighbors anyway
	//but note that we have to deal with alignments that have 2*fertility[0] > curJ

	best_prob = compute_ehmm_viterbi_alignment(cur_source,cur_lookup,cur_target,dict_,
						   wrapper->align_model_[curI-1], wrapper->initial_prob_[curI-1],
						   best_known_alignment_[s], wrapper->hmm_options_.align_type_,
						   false, false);

	make_alignment_feasible(cur_source, cur_target, cur_lookup, best_known_alignment_[s]);

	for (uint j=0; j < curJ; j++)
	  fertility[best_known_alignment_[s][j]]++;

	if (hillclimb_mode_ == HillclimbingRestart) 
	  initial_alignment[s] = best_known_alignment_[s]; //since before nothing useful was set

	//NOTE: to be 100% proper we should recalculate the prob of the alignment if it was corrected
	//(would need to convert the alignment to internal mode first). But this only affects the energy printout at the end of 
	// the iteration
      }
      else {
	best_prob = update_alignment_by_hillclimbing(cur_source,cur_target,cur_lookup,sum_iter,fertility,
						     expansion_move_prob,swap_move_prob,best_known_alignment_[s]);
      }
      max_perplexity -= std::log(best_prob);

      tHillclimbEnd = std::clock();

      hillclimbtime += diff_seconds(tHillclimbEnd,tHillclimbStart);

      /**** update empty word counts *****/
      
      fzero_count += fertility[0];
      fnonzero_count += curJ - 2*fertility[0];

      /**** update fertility counts *****/
      for (uint i=1; i <= curI; i++) {

        const uint cur_fert = fertility[i];
        const uint t_idx = cur_target[i-1];

        ffert_count[t_idx][cur_fert] += 1.0;
      }

      /**** update dictionary counts *****/
      for (uint j=0; j < curJ; j++) {

        const uint s_idx = cur_source[j];
        const uint cur_aj = best_known_alignment_[s][j];

        if (cur_aj != 0) {
          fwcount[cur_target[cur_aj-1]][cur_lookup(j,cur_aj-1)] += 1.0;
        }
        else {
          fwcount[0][s_idx-1] += 1.0;
        }
      }

      /**** update distortion counts *****/

      NamedStorage1D<std::vector<ushort> > aligned_source_words(curI+1,MAKENAME(aligned_source_words));
      for (uint j=0; j < curJ; j++)
	aligned_source_words[best_known_alignment_[s][j]].push_back(j);

      {

	uint prev_center = MAX_UINT;

	Storage1D<bool> fixed(curJ,false);

	uint nOpen = curJ;

	for (uint i=1; i <= curI; i++) {
	  
	  if (fertility[i] > 0) {
	   
	    const std::vector<ushort>& cur_aligned_source_words = aligned_source_words[i];
	    assert(cur_aligned_source_words.size() == fertility[i]);

	    //a) head of the cept
	    const uint first_j = cur_aligned_source_words[0];

	    if (prev_center != MAX_UINT) { // currently not estimating a start probability
	      
	      const uint sclass = source_class_[cur_source[first_j]];

	      const uint nAvailable = nOpen - (cur_aligned_source_words.size()-1);

	      Math3D::Tensor<double>& cur_inter_distortion_count = inter_distortion_count[nAvailable];

	      uint pos_first_j = MAX_UINT;
	      uint pos_prev_center = MAX_UINT;
	      
	      uint nCurOpen = 0;
	      for (uint j = 0; j <= std::max(first_j,prev_center); j++) {
		
		if (j == first_j)
		  pos_first_j = nCurOpen;
		
		if (!fixed[j])
		  nCurOpen++;
		
		if (j == prev_center)
		  pos_prev_center = nCurOpen;
	      }
	      
	      cur_inter_distortion_count(pos_first_j,pos_prev_center,sclass) += 1.0;
	    }
	    else if (use_sentence_start_prob_) {
	      sentence_start_count[curJ][first_j] += 1.0;
	    }

	    fixed[first_j] = true;
	    nOpen--;

	    //b) body of the cept
	    for (uint k=1; k < cur_aligned_source_words.size(); k++) {

	      const uint cur_j = cur_aligned_source_words[k];

	      const uint sclass = source_class_[cur_source[cur_j]];

	      uint pos = MAX_UINT;

	      uint nAvailable = 0;
	      for (uint j = cur_aligned_source_words[k-1]+1; j < curJ; j++) {
		
		if (j == cur_j)
		  pos = nAvailable;
		
		if (!fixed[j])
		  nAvailable++;
	      }

	      nAvailable -= cur_aligned_source_words.size() - 1 - k;
	      
	      intra_distortion_count[nAvailable](pos,sclass) += 1.0;

	      fixed[cur_j] = true;
	      nOpen--;
	    }

	    //c) calculate the center of the cept
	    switch (cept_start_mode_) {
	    case IBM4CENTER : {
	      
	      //compute the center of this cept and store the result in prev_cept_center
	      double sum = 0.0;
	      for (uint k=0; k < cur_aligned_source_words.size(); k++) {
		sum += cur_aligned_source_words[k];
	      }
	      
	      prev_center = (int) round(sum / cur_aligned_source_words.size());
	      break;
	    }
	    case IBM4FIRST:
	      prev_center = first_j;
	      break;
	    case IBM4LAST:
	      prev_center = cur_aligned_source_words.back();
	      break;
	    case IBM4UNIFORM:
	      break;
	    default:
	      assert(false);
	    }

	  }
	}
      }
    
    } //loop over sentences finished

    
    /***** update probability models from counts *******/

    //update p_zero_ and p_nonzero_
    if (!fix_p0_) {
      std::cerr << "zero counts: " << fzero_count << ", " << fnonzero_count << std::endl;
      double fsum = fzero_count + fnonzero_count;
      p_zero_ = fzero_count / fsum;
      p_nonzero_ = fnonzero_count / fsum;
    }

    std::cerr << "new p_zero: " << p_zero_ << std::endl;

    assert(!isnan(p_zero_));
    assert(!isnan(p_nonzero_));

    //DEBUG
    uint nZeroAlignments = 0;
    uint nAlignments = 0;
    for (size_t s=0; s < source_sentence_.size(); s++) {

      nAlignments += source_sentence_[s].size();

      for (uint j=0; j < source_sentence_[s].size(); j++) {
        if (best_known_alignment_[s][j] == 0)
          nZeroAlignments++;
      }
    }
    std::cerr << "percentage of zero-aligned words: " 
              << (((double) nZeroAlignments) / ((double) nAlignments)) << std::endl;
    //END_DEBUG

    //update dictionary
    for (uint i=0; i < nTargetWords; i++) {

      const double sum = fwcount[i].sum();
	
      if (sum > 1e-305) {
	double inv_sum = 1.0 / sum;
	
	if (isnan(inv_sum)) {
	  std::cerr << "invsum " << inv_sum << " for target word #" << i << std::endl;
	  std::cerr << "sum = " << fwcount[i].sum() << std::endl;
	  std::cerr << "number of cooccuring source words: " << fwcount[i].size() << std::endl;
	}
	
	assert(!isnan(inv_sum));
	
	for (uint k=0; k < fwcount[i].size(); k++) {
	  dict_[i][k] = fwcount[i][k] * inv_sum;
	}
      }
      else {
	//std::cerr << "WARNING: did not update dictionary entries because the sum was " << sum << std::endl;
      }
    }

    //update inter distortion probabilities
    if (nonpar_distortion_) {

      for (uint J=1; J < inter_distortion_count.size(); J++) {

	for (uint y=0; y < inter_distortion_count[J].yDim(); y++) {
	  for (uint z=0; z < inter_distortion_count[J].zDim(); z++) {
	    
	    double sum = 0.0;
	    for (uint j=0; j < inter_distortion_count[J].xDim(); j++)
	      sum += inter_distortion_count[J](j,y,z);
	    
	    if (sum > 1e-305) {
	      
	      for (uint j=0; j < inter_distortion_count[J].xDim(); j++)
		inter_distortion_prob_[J](j,y,z) = std::max(1e-8,inter_distortion_count[J](j,y,z) / sum);
	    }
	  }
	}
      }
    }
    else {

      Math2D::Matrix<double> hyp_param = inter_distortion_param_;   
  
      for (uint s=0; s < inter_distortion_param_.yDim(); s++) {

	inter_distortion_m_step(s, inter_distortion_count);
      }

      par2nonpar_inter_distortion();
    }

    //update intra distortion probabilities
    if (nonpar_distortion_) {

      for (uint J=1; J < intra_distortion_prob_.size(); J++) {
	
	for (uint s=0; s < intra_distortion_prob_[J].yDim(); s++) {

	  double sum = 0.0;
	  for (uint j=0; j < J; j++)
	    sum += intra_distortion_count[J](j,s);
	  
	  if (sum > 1e-305) {
	    for (uint j=0; j < J; j++)
	      intra_distortion_prob_[J](j,s) = std::max(1e-8,intra_distortion_count[J](j,s) / sum);
	  }
	}
      }
    }
    else {
      

      Math2D::Matrix<double> hyp_param = intra_distortion_param_;
      
      // call m-steps
      for (uint s=0; s < intra_distortion_param_.yDim(); s++) {
	
	intra_distortion_m_step(s,intra_distortion_count);
      }

      par2nonpar_intra_distortion();
    }

    if (use_sentence_start_prob_) {
      start_prob_m_step(sentence_start_count, sentence_start_parameters_);
      par2nonpar_start_prob(sentence_start_parameters_,sentence_start_prob_);
    }

    if (fert_trainer == 0 && wrapper == 0) { // no point doing ICM in a transfer iteration 


      std::cerr << "starting ICM" << std::endl;

      const double log_pzero = std::log(p_zero_);
      const double log_pnonzero = std::log(p_nonzero_);
      
      Math1D::NamedVector<uint> dict_sum(fwcount.size(),MAKENAME(dict_sum));
      for (uint k=0; k < fwcount.size(); k++)
	dict_sum[k] = fwcount[k].sum();
      
      uint nSwitches = 0;
      
      for (size_t s=0; s < nSentences; s++) {
	
	if ((s% 10000) == 0)
	  std::cerr << "sentence pair #" << s << std::endl;
	
	const Storage1D<uint>& cur_source = source_sentence_[s];
	const Storage1D<uint>& cur_target = target_sentence_[s];
	const SingleLookupTable& cur_lookup = get_wordlookup(cur_source,cur_target,wcooc_,
							     nSourceWords_,slookup_[s],aux_lookup);
	
	Math1D::Vector<AlignBaseType>& cur_best_known_alignment = best_known_alignment_[s];
	
	const uint curI = cur_target.size();
	const uint curJ = cur_source.size();
	
	Math1D::NamedVector<uint> fertility(curI+1,0,MAKENAME(fertility));
	
	for (uint j=0; j < curJ; j++)
	  fertility[best_known_alignment_[s][j]]++;
	
	NamedStorage1D<std::vector<AlignBaseType> > hyp_aligned_source_words(curI+1,MAKENAME(hyp_aligned_source_words));
	
	for (uint j=0; j < curJ; j++) {
	  
	  uint aj = cur_best_known_alignment[j];
	  hyp_aligned_source_words[aj].push_back(j);
	}

	double cur_distort_prob = distortion_prob(cur_source,cur_target,hyp_aligned_source_words);
	
	for (uint j=0; j < curJ; j++) {

	  for (uint i = 0; i <= curI; i++) {
	    
	    const uint cur_aj = best_known_alignment_[s][j];
	    const uint cur_word = (cur_aj == 0) ? 0 : cur_target[cur_aj-1];
	    
	    const uint new_target_word = (i == 0) ? 0 : cur_target[i-1];

	    /**** dict ***/
	    
	    bool allowed = (cur_aj != i && (i != 0 || 2*fertility[0]+2 <= curJ));
	    
	    if (i != 0 && (fertility[i]+1) > fertility_limit_)
	      allowed = false;
	    
	    if (allowed) {

	      Math1D::Vector<double>& cur_fert_count = ffert_count[cur_word];
	      Math1D::Vector<double>& hyp_fert_count = ffert_count[new_target_word];
	      
	      hyp_aligned_source_words[cur_aj].erase(std::find(hyp_aligned_source_words[cur_aj].begin(),
	       						       hyp_aligned_source_words[cur_aj].end(),j));
	      hyp_aligned_source_words[i].push_back(j);
	      vec_sort(hyp_aligned_source_words[i]);
	      
	      double change = 0.0;
	      
	      Math1D::Vector<double>& cur_dictcount = fwcount[cur_word]; 
	      Math1D::Vector<double>& hyp_dictcount = fwcount[new_target_word]; 
	      
	      const uint cur_idx = (cur_aj == 0) ? cur_source[j]-1 : cur_lookup(j,cur_aj-1);
	      
	      const uint hyp_idx = (i == 0) ? cur_source[j]-1 : cur_lookup(j,i-1);

	      change += common_icm_change(fertility, log_pzero, log_pnonzero, dict_sum, cur_dictcount, hyp_dictcount,
					  cur_fert_count, hyp_fert_count, cur_word, new_target_word, cur_idx, hyp_idx,
					  cur_aj, i, curJ);
	    
	      /***** distortion ****/
	      change -= - std::log(cur_distort_prob);
	      
	      const long double hyp_distort_prob = distortion_prob(cur_source,cur_target,hyp_aligned_source_words);
	      change += - std::log(hyp_distort_prob);
	      
	      if (change < -0.01) {
		
		cur_best_known_alignment[j] = i;
		
		nSwitches++;
		
		//dict
		cur_dictcount[cur_idx] -= 1;
		hyp_dictcount[hyp_idx] += 1;
		dict_sum[cur_word] -= 1;
		dict_sum[new_target_word] += 1;
		
		//fert
		if (cur_word != 0) {
		  uint prev_fert = fertility[cur_aj];
		  assert(prev_fert != 0);
		  cur_fert_count[prev_fert] -= 1;
		  cur_fert_count[prev_fert-1] += 1;
		}
		if (new_target_word != 0) {
		  uint prev_fert = fertility[i];
		  hyp_fert_count[prev_fert] -= 1;
		  hyp_fert_count[prev_fert+1] += 1;
		}
		
		fertility[cur_aj]--;
		fertility[i]++;
		
		cur_distort_prob = hyp_distort_prob; 
	      }
	      else {
		
		hyp_aligned_source_words[i].erase(std::find(hyp_aligned_source_words[i].begin(),
							    hyp_aligned_source_words[i].end(),j));
		hyp_aligned_source_words[cur_aj].push_back(j);
		vec_sort(hyp_aligned_source_words[cur_aj]);
	      }
	    }
	  }	 
	}
      } //ICM-loop over sentences finished

      std::cerr << nSwitches << " changes in ICM stage" << std::endl; 
    }

    //update fertility probabilities
    update_fertility_prob(ffert_count,0.0);

    //update dictionary
    for (uint i=0; i < nTargetWords; i++) {

      const double sum = fwcount[i].sum();
	
      if (sum > 1e-305) {
	double inv_sum = 1.0 / sum;
	
	if (isnan(inv_sum)) {
	  std::cerr << "invsum " << inv_sum << " for target word #" << i << std::endl;
	  std::cerr << "sum = " << fwcount[i].sum() << std::endl;
	  std::cerr << "number of cooccuring source words: " << fwcount[i].size() << std::endl;
	}
	
	assert(!isnan(inv_sum));
	
	for (uint k=0; k < fwcount[i].size(); k++) {
	  dict_[i][k] = fwcount[i][k] * inv_sum;
	}
      }
      else {
	//std::cerr << "WARNING: did not update dictionary entries because the sum was " << sum << std::endl;
      }
    }

    //TODO: think about whether to update distortions parameters here as well

    std::string transfer = ((fert_trainer != 0 || wrapper != 0) && iter == 1) ? " (transfer) " : ""; 

    max_perplexity /= source_sentence_.size();

    std::cerr << "IBM-5 max-perplex-energy in between iterations #" << (iter-1) << " and " << iter << transfer << ": "
              << max_perplexity << std::endl;
    if (possible_ref_alignments_.size() > 0) {
      
      std::cerr << "#### IBM-5-AER in between iterations #" << (iter-1) << " and " << iter << transfer << ": " << AER() << std::endl;
      std::cerr << "#### IBM-5-fmeasure in between iterations #" << (iter-1) << " and " << iter << transfer << ": " << f_measure() << std::endl;
      std::cerr << "#### IBM-5-DAE/S in between iterations #" << (iter-1) << " and " << iter << transfer << ": " 
                << DAE_S() << std::endl;
    }

    std::cerr << (((double) sum_iter) / source_sentence_.size()) << " average hillclimbing iterations per sentence pair" 
              << std::endl;

  }


  if (nonpar_distortion_) {

    //we still update <code> inter_distortion_param_ </code>  and <code> intra_distortion_param_ </code>
    //so that we can use them when computing external alignments


    //a) inter
    inter_distortion_param_.set_constant(0.0);    

    for (uint s=0; s < inter_distortion_param_.yDim(); s++) {
      
      for (uint J=0; J < inter_distortion_count.size(); J++) {
	
	for (uint prev_pos = 0; prev_pos < inter_distortion_count[J].yDim(); prev_pos++) {
	  
	  for (uint j=0; j < J; j++) {
	  
	    double cur_weight = inter_distortion_count[J](j,prev_pos,s);
	    inter_distortion_param_(displacement_offset_+j-prev_pos,s) += cur_weight;
	  }
	}
      }
      
      double sum = 0.0;
      for (uint j=0; j < inter_distortion_param_.xDim(); j++)
	sum += inter_distortion_param_(j,s);
      
      if (sum > 1e-305) {
	
	for (uint j=0; j < inter_distortion_param_.xDim(); j++)
	  inter_distortion_param_(j,s) = std::max(1e-8,inter_distortion_param_(j,s)/sum);
      }
      else {
	for (uint j=0; j < inter_distortion_param_.xDim(); j++)
	  inter_distortion_param_(j,s) = 1.0 / inter_distortion_param_.xDim();
      }
    }      

    //b) intra
    intra_distortion_param_.set_constant(0.0);    

    for (uint s=0; s < intra_distortion_param_.yDim(); s++) {

      for (uint J=0; J < intra_distortion_count.size(); J++) {
      
	for (uint j=0; j < J; j++)
	  intra_distortion_param_(j,s) += intra_distortion_count[J](j,s);
      }

      double sum = 0.0;
      for (uint j=0; j < intra_distortion_param_.xDim(); j++)
	sum += intra_distortion_param_(j,s);

      if (sum > 1e-305) {

	for (uint j=0; j < intra_distortion_param_.xDim(); j++)
	  intra_distortion_param_(j,s) = std::max(1e-8,intra_distortion_param_(j,s) / sum);
      }
      else {
      
	for (uint j=0; j < intra_distortion_param_.xDim(); j++)
	  intra_distortion_param_(j,s) = 1.0 / intra_distortion_param_.xDim();
      }
    }
  }

  iter_offs_ = iter-1;
}

/* virtual */
void IBM5Trainer::prepare_external_alignment(const Storage1D<uint>& source, const Storage1D<uint>& target,
					     const SingleLookupTable& lookup, Math1D::Vector<AlignBaseType>& alignment) {

  const uint J = source.size();

  common_prepare_external_alignment(source,target,lookup,alignment);


  /*** check if distortion tables are large enough ***/

  if (J > maxJ_) {

    Math2D::Matrix<double> new_inter_param(2*J+1,nSourceClasses_,1e-8);    

    for (uint s=0; s < inter_distortion_param_.yDim(); s++) {

      for (int j = -int(maxJ_); j <= int(maxJ_); j++)
	new_inter_param(j+J,s) = inter_distortion_param_(j+displacement_offset_,s);
    }

    displacement_offset_ = J;

    inter_distortion_param_ = new_inter_param;
    inter_distortion_prob_.resize(J+1);

    for (uint JJ=1; JJ < inter_distortion_prob_.size(); JJ++) {
    
      if (inter_distortion_prob_[JJ].yDim() < J)
	inter_distortion_prob_[JJ].resize(inter_distortion_prob_[JJ].xDim(),J,nSourceClasses_,1e-8);
    }


    for (uint JJ=1; JJ <= J; JJ++) {

      uint prev_yDim = inter_distortion_prob_[JJ].yDim();

      inter_distortion_prob_[JJ].resize(JJ,J,nSourceClasses_,1e-8);

      if (nonpar_distortion_) { //otherwise the update is done by par2nonpar_.. below

	for (uint y=prev_yDim; y < inter_distortion_prob_[JJ].yDim(); y++) {
	  for (uint z=0; z < inter_distortion_prob_[JJ].zDim(); z++) {
	    
	    double sum = 0.0;
	    for (int j=0; j < int(inter_distortion_prob_[JJ].xDim()); j++)
	      sum += inter_distortion_param_(j-y+displacement_offset_,z);
	    
	    assert(sum > 1e-305);

	    if (sum > 1e-305) {
	      
	      for (uint j=0; j < inter_distortion_prob_[JJ].xDim(); j++)
		inter_distortion_prob_[JJ](j,y,z) = std::max(1e-8,inter_distortion_param_(j-y+displacement_offset_,z) / sum);
	    }
	  }
	}
      }
    }

    if (!nonpar_distortion_)
      par2nonpar_inter_distortion();


    Math2D::Matrix<double> new_intra_param(J,nSourceClasses_,1e-8);
    for (uint s=0; s < intra_distortion_param_.yDim(); s++)
      for (uint j=0; j < intra_distortion_param_.xDim(); j++)
	new_intra_param(j,s) = intra_distortion_param_(j,s);

    intra_distortion_param_ = new_intra_param;
    intra_distortion_prob_.resize(J+1);

    for (uint JJ=1; JJ <= J; JJ++) {

      intra_distortion_prob_[JJ].resize(JJ,nSourceClasses_,1e-8);

      if (nonpar_distortion_ && JJ > maxJ_) { //for parametric distortion the update is done by par2nonpar_... below

	for (uint s=0; s < intra_distortion_param_.yDim(); s++) {

	  double sum = 0.0;
	  for (uint j=0; j < JJ; j++)
	    sum += intra_distortion_param_(j,s);

	  for (uint j=0; j < JJ; j++)
	    intra_distortion_prob_[JJ](j,s) = intra_distortion_param_(j,s) / sum;
	}
      }
    }
    
    if (!nonpar_distortion_)
      par2nonpar_intra_distortion();

    maxJ_ = J;
  }
  
  if (use_sentence_start_prob_) {

    if (sentence_start_prob_.size() <= J) {
      sentence_start_prob_.resize(J+1);
    }

    if (sentence_start_prob_[J].size() < J) {
      sentence_start_prob_[J].resize(J,1.0 / J);
    }

    par2nonpar_start_prob(sentence_start_parameters_,sentence_start_prob_);
  }


}
