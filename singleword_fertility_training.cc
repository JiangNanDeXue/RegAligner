/*** first version written by Thomas Schoenemann as a private person without employment, November+December 2009 ***/
/*** continued by Thomas Schoenemann as an employee of Lund University, Sweden, Febuary 2010 ****/

#include "singleword_fertility_training.hh"
#include "alignment_computation.hh"
#include "hmm_forward_backward.hh"
#include "combinatoric.hh"
#include "tensor.hh"
#include "alignment_error_rate.hh"
#include "timing.hh"
#include "ibm1_training.hh"
#include "projection.hh"

#ifdef HAS_CBC
#include "sparse_matrix_description.hh"
#include "ClpSimplex.hpp"
#include "CbcModel.hpp"
#include "OsiClpSolverInterface.hpp"

#include "CglGomory/CglGomory.hpp"
#include "CglProbing/CglProbing.hpp"
#include "CglRedSplit/CglRedSplit.hpp"
#include "CglTwomir/CglTwomir.hpp"
#include "CglMixedIntegerRounding/CglMixedIntegerRounding.hpp"
#include "CglMixedIntegerRounding2/CglMixedIntegerRounding2.hpp"
#include "CglOddHole/CglOddHole.hpp"
#include "CglLandP/CglLandP.hpp"
#include "CglClique/CglClique.hpp"
#include "CglStored.hpp"

#include "CbcHeuristic.hpp"
#endif

#include <fstream>
#include <set>

/************* implementation of FertilityModelTrainer *******************************/

FertilityModelTrainer::FertilityModelTrainer(const Storage1D<Storage1D<uint> >& source_sentence,
					     const Storage1D<Math2D::Matrix<uint> >& slookup,
					     const Storage1D<Storage1D<uint> >& target_sentence,
					     SingleWordDictionary& dict,
					     const CooccuringWordsType& wcooc,
					     uint nSourceWords, uint nTargetWords,
					     const std::map<uint,std::set<std::pair<uint,uint> > >& sure_ref_alignments,
					     const std::map<uint,std::set<std::pair<uint,uint> > >& possible_ref_alignments) :
  uncovered_set_(MAKENAME(uncovered_sets_)), predecessor_sets_(MAKENAME(predecessor_sets_)), 
  nUncoveredPositions_(MAKENAME(nUncoveredPositions_)), j_before_end_skips_(MAKENAME(j_before_end_skips_)),
  first_set_(MAKENAME(first_set_)), next_set_idx_(0), coverage_state_(MAKENAME(coverage_state_)),
  first_state_(MAKENAME(first_state_)), predecessor_coverage_states_(MAKENAME(predecessor_coverage_states_)),
  source_sentence_(source_sentence), slookup_(slookup), target_sentence_(target_sentence), 
  wcooc_(wcooc), dict_(dict), nSourceWords_(nSourceWords), nTargetWords_(nTargetWords),
  fertility_prob_(nTargetWords,MAKENAME(fertility_prob_)), 
  best_known_alignment_(MAKENAME(best_known_alignment_)),
  sure_ref_alignments_(sure_ref_alignments), possible_ref_alignments_(possible_ref_alignments)
{

  Math1D::Vector<uint> max_fertility(nTargetWords,0);

  maxJ_ = 0;
  maxI_ = 0;

  for (uint s=0; s < source_sentence.size(); s++) {

    const uint curJ = source_sentence[s].size();
    const uint curI = target_sentence[s].size();

    if (maxJ_ < curJ)
      maxJ_ = curJ;
    if (maxI_ < curI)
      maxI_ = curI;

    if (max_fertility[0] < curJ)
      max_fertility[0] = curJ;

    for (uint i = 0; i < curI; i++) {

      const uint t_idx = target_sentence[s][i];

      if (max_fertility[t_idx] < curJ)
	max_fertility[t_idx] = curJ;
    }
  }
  
  for (uint i=0; i < nTargetWords; i++) {
    fertility_prob_[i].resize_dirty(max_fertility[i]+1);
    fertility_prob_[i].set_constant(1.0 / (max_fertility[i]+1));
  }

  best_known_alignment_.resize(source_sentence.size());
  for (uint s=0; s < source_sentence.size(); s++)
    best_known_alignment_[s].resize(source_sentence[s].size(),0);

  //compute_uncovered_sets(3);
}

const NamedStorage1D<Math1D::Vector<double> >& FertilityModelTrainer::fertility_prob() const {
  return fertility_prob_;
}

const NamedStorage1D<Math1D::Vector<ushort> >& FertilityModelTrainer::best_alignments() const {
  return best_known_alignment_;
}

double FertilityModelTrainer::AER() {

  double sum_aer = 0.0;
  uint nContributors = 0;
  
  for (uint s=0; s < source_sentence_.size(); s++) {
    
    if (possible_ref_alignments_.find(s+1) != possible_ref_alignments_.end()) {
      
      nContributors++;
      //compute viterbi alignment
      //add alignment error rate
      Math1D::Vector<uint> uint_alignment(best_known_alignment_[s].size());
      for (uint k=0; k < best_known_alignment_[s].size(); k++)
	uint_alignment[k] = best_known_alignment_[s][k];

      sum_aer += ::AER(uint_alignment,sure_ref_alignments_[s+1],possible_ref_alignments_[s+1]);
    }
  }
  
  sum_aer *= 100.0 / nContributors;
  return sum_aer;
}

double FertilityModelTrainer::AER(const Storage1D<Math1D::Vector<ushort> >& alignments) {

  double sum_aer = 0.0;
  uint nContributors = 0;
  
  for (uint s=0; s < source_sentence_.size(); s++) {
    
    if (possible_ref_alignments_.find(s+1) != possible_ref_alignments_.end()) {
      
      nContributors++;
      //compute viterbi alignment
      //add alignment error rate
      Math1D::Vector<uint> uint_alignment(best_known_alignment_[s].size());
      for (uint k=0; k < best_known_alignment_[s].size(); k++)
	uint_alignment[k] = best_known_alignment_[s][k];

      sum_aer += ::AER(uint_alignment,sure_ref_alignments_[s+1],possible_ref_alignments_[s+1]);
    }
  }
  
  sum_aer *= 100.0 / nContributors;
  return sum_aer;
}

double FertilityModelTrainer::f_measure(double alpha) {

  double sum_fmeasure = 0.0;
  uint nContributors = 0;
  
  for (uint s=0; s < source_sentence_.size(); s++) {
    
    if (possible_ref_alignments_.find(s+1) != possible_ref_alignments_.end()) {
      
      nContributors++;
      //compute viterbi alignment
      //add alignment error rate
      Math1D::Vector<uint> uint_alignment(best_known_alignment_[s].size());
      for (uint k=0; k < best_known_alignment_[s].size(); k++)
	uint_alignment[k] = best_known_alignment_[s][k];

      sum_fmeasure += ::f_measure(uint_alignment,sure_ref_alignments_[s+1],possible_ref_alignments_[s+1], alpha);
    }
  }
  
  sum_fmeasure /= nContributors;
  return sum_fmeasure;
}

double FertilityModelTrainer::DAE_S() {

  double sum_errors = 0.0;
  uint nContributors = 0;
  
  for (uint s=0; s < source_sentence_.size(); s++) {
    
    if (possible_ref_alignments_.find(s+1) != possible_ref_alignments_.end()) {
      
      nContributors++;
      //compute viterbi alignment
      //add alignment error rate
      Math1D::Vector<uint> uint_alignment(best_known_alignment_[s].size());
      for (uint k=0; k < best_known_alignment_[s].size(); k++)
	uint_alignment[k] = best_known_alignment_[s][k];

      sum_errors += ::nDefiniteAlignmentErrors(uint_alignment,sure_ref_alignments_[s+1],possible_ref_alignments_[s+1]);
    }
  }
  
  sum_errors /= nContributors;
  return sum_errors;
}

void FertilityModelTrainer::print_uncovered_set(uint state) const {

  for (uint k=0; k < uncovered_set_.xDim(); k++) {

    if (uncovered_set_(k,state) == MAX_USHORT)
      std::cerr << "-";
    else
      std::cerr << uncovered_set_(k,state);
    std::cerr << ",";
  }
}

uint FertilityModelTrainer::nUncoveredPositions(uint state) const {

  uint result = uncovered_set_.xDim();

  for (uint k=0; k < uncovered_set_.xDim(); k++) {
    if (uncovered_set_(k,state) == MAX_USHORT)
      result--;
    else
      break;
  }

  return result;
}

void FertilityModelTrainer::cover(uint level) {

  //  std::cerr << "*****cover(" << level << ")" << std::endl;

  if (level == 0) {
    next_set_idx_++;
    return;
  }

  const uint ref_set_idx = next_set_idx_;

  next_set_idx_++; //to account for sets which are not fully filled
  assert(next_set_idx_ <= uncovered_set_.yDim());

  const uint ref_j = uncovered_set_(level,ref_set_idx);
  //std::cerr << "ref_j: " << ref_j << std::endl;
  //std::cerr << "ref_line: ";
//   for (uint k=0; k < uncovered_set_.xDim(); k++) {

//     if (uncovered_set_(k,ref_set_idx) == MAX_USHORT)
//       std::cerr << "-";
//     else
//       std::cerr << uncovered_set_(k,ref_set_idx);
//     std::cerr << ",";
//   }
//   std::cerr << std::endl;
  

  for (uint j=1; j < ref_j; j++) {
    
    //std::cerr << "j: " << j << std::endl;

    assert(next_set_idx_ <= uncovered_set_.yDim());
    
    for (uint k=level; k < uncovered_set_.xDim(); k++)
      uncovered_set_(k,next_set_idx_) = uncovered_set_(k,ref_set_idx);

    uncovered_set_(level-1,next_set_idx_) = j;
    
    cover(level-1);
  }
}

void FertilityModelTrainer::compute_uncovered_sets(uint nMaxSkips) {

  uint nSets = choose(maxJ_,nMaxSkips);
  for (int k= nMaxSkips-1; k >= 0; k--)
    nSets += choose(maxJ_,k);
  std::cerr << nSets << " sets of uncovered positions" << std::endl;

  uncovered_set_.resize_dirty(nMaxSkips,nSets);
  uncovered_set_.set_constant(MAX_USHORT);
  nUncoveredPositions_.resize_dirty(nSets);
  first_set_.resize_dirty(maxJ_+2);
  
  next_set_idx_ = 1; //the first set contains no uncovered positions at all

  for (uint j=1; j <= maxJ_; j++) {
    
    first_set_[j] = next_set_idx_;
    uncovered_set_(nMaxSkips-1,next_set_idx_) = j;
   
    cover(nMaxSkips-1);
  }
  first_set_[maxJ_+1] = next_set_idx_;

  assert(nSets == next_set_idx_);

  std::cerr << next_set_idx_ << " states." << std::endl;

  predecessor_sets_.resize(nSets);
  j_before_end_skips_.resize(nSets);

  for (uint state=0; state < nSets; state++) {
    nUncoveredPositions_[state] = nUncoveredPositions(state);

    uint j_before_end_skips = (state == 0) ? 0 : (uncovered_set_(nMaxSkips-1,state) - 1);
    for (int k=nMaxSkips-2; k >= ((int) (nMaxSkips-nUncoveredPositions_[state])); k--) {
      
      if (uncovered_set_(k,state)+1 == uncovered_set_(k+1,state)) {
	j_before_end_skips = uncovered_set_(k,state)-1;
      }
      else
	break;
    }

    j_before_end_skips_[state] = j_before_end_skips;
  }

  uint nMaxPredecessors = 0;
  
  for (uint state = 0; state < next_set_idx_; state++) {

    std::vector<std::pair<uint,uint> > cur_predecessor_sets;

//     std::cerr << "processing state ";
//     for (uint k=0; k < nMaxSkips; k++) {

//       if (uncovered_set_(k,state) == MAX_USHORT)
// 	std::cerr << "-";
//       else
// 	std::cerr << uncovered_set_(k,state);
//       std::cerr << ",";
//     }
//     std::cerr << std::endl;

    //uint maxUncoveredPos = uncovered_set_(nMaxSkips-1,state);

    //NOTE: a state is always its own predecessor state; to save memory we omit the entry
    bool limit_state = (uncovered_set_(0,state) != MAX_USHORT);
    //uint prev_candidate;

    if (limit_state) {
//       for (uint k=1; k < nMaxSkips; k++)
// 	assert(uncovered_set_(k,state) != MAX_USHORT);

      //predecessor states can only be states with less entries

      uint nConsecutiveEndSkips = 1;
      for (int k=nMaxSkips-2; k >= 0; k--) {
	
	if (uncovered_set_(k,state) == uncovered_set_(k+1,state) - 1)
	  nConsecutiveEndSkips++;
	else
	  break;
      }
      const uint nPrevSkips = nMaxSkips-nConsecutiveEndSkips;
      const uint highestUncoveredPos = uncovered_set_(nMaxSkips-1,state);

      assert(nMaxSkips >= 2); //TODO: handle the cases of nMaxSkips = 1 or 0

      if (nConsecutiveEndSkips == nMaxSkips)
	cur_predecessor_sets.push_back(std::make_pair(0,highestUncoveredPos+1));
      else {

	const uint skip_before_end_skips = uncovered_set_(nMaxSkips-nConsecutiveEndSkips-1,state);
	
	for (uint prev_candidate = first_set_[skip_before_end_skips]; 
	     prev_candidate < first_set_[skip_before_end_skips+1]; prev_candidate++) {

	  if (nUncoveredPositions_[prev_candidate] == nPrevSkips) {

	    bool is_predecessor = true;
	    
	    for (uint k=0; k < nPrevSkips; k++) {
	      if (uncovered_set_(k+nConsecutiveEndSkips,prev_candidate) != uncovered_set_(k,state)) {
		is_predecessor = false;
		break;
	      }
	    }

	    if (is_predecessor) {
	      cur_predecessor_sets.push_back(std::make_pair(prev_candidate,highestUncoveredPos+1));

	      break;
	    }
	    
	  }
	}
      }

// #if 0
//       assert(nMaxSkips >= 2); //TODO: handle the cases of nMaxSkips = 1 or 0

//       const uint highestUncoveredPos = uncovered_set_(nMaxSkips-1,state);
//       const uint secondHighestUncoveredPos = uncovered_set_(nMaxSkips-2,state);

//       bool is_predecessor;
//       for (prev_candidate = 0; prev_candidate < first_set_[secondHighestUncoveredPos+1]; prev_candidate++) {

// 	is_predecessor = true;
// 	if (uncovered_set_(0,prev_candidate) != MAX_USHORT)
// 	  is_predecessor = false;
// 	else {
// 	  const uint nCandidateSkips = nUncoveredPositions_[prev_candidate];
// 	  const uint nNewSkips = nMaxSkips-nCandidateSkips;
	  
// 	  if (nNewSkips != nConsecutiveEndSkips)
// 	    is_predecessor = false;
// 	  else {
// 	    for (uint k=0; k < nCandidateSkips; k++) {
// 	      if (uncovered_set_(k+nNewSkips,prev_candidate) != uncovered_set_(k,state)) {
// 		is_predecessor = false;
// 		break;
// 	      }
// 	    }
// 	  }
// 	}

// 	if (is_predecessor) {
// 	  cur_predecessor_sets.push_back(std::make_pair(prev_candidate,highestUncoveredPos+1));
// 	}
//       }
// #endif
    }
    else {

      //predecessor entries can be states with less entries 
      // or states with more entries

      const uint nUncoveredPositions = nUncoveredPositions_[state];

      uint nConsecutiveEndSkips = (state == 0) ? 0 : 1;
      for (int k=nMaxSkips-2; k >= ((int) (nMaxSkips-nUncoveredPositions)); k--) {
	
	if (uncovered_set_(k,state) == uncovered_set_(k+1,state) - 1)
	  nConsecutiveEndSkips++;
	else
	  break;
      }
      const uint nPrevSkips = nUncoveredPositions -nConsecutiveEndSkips; 
      const uint highestUncoveredPos = uncovered_set_(nMaxSkips-1,state);
      
      //a) find states with less entries
      if (nUncoveredPositions == nConsecutiveEndSkips) {
	if (state != 0)
	  cur_predecessor_sets.push_back(std::make_pair(0,highestUncoveredPos+1));
      }
      else {

	assert(state != 0);

	const uint skip_before_end_skips = uncovered_set_(nMaxSkips-nConsecutiveEndSkips-1,state);
	
	for (uint prev_candidate = first_set_[skip_before_end_skips]; 
	     prev_candidate < first_set_[skip_before_end_skips+1]; prev_candidate++) {

	  if (nUncoveredPositions_[prev_candidate] == nPrevSkips) {

	    bool is_predecessor = true;
	    
	    for (uint k=nMaxSkips-nUncoveredPositions; 
		 k < nMaxSkips - nUncoveredPositions + nPrevSkips; k++) {
	      if (uncovered_set_(k+nConsecutiveEndSkips,prev_candidate) != uncovered_set_(k,state)) {
		is_predecessor = false;
		break;
	      }
	    }

	    if (is_predecessor) {
	      cur_predecessor_sets.push_back(std::make_pair(prev_candidate,highestUncoveredPos+1));
	      break;
	    }
	    
	  }
	}

// #if 0
// 	bool match;
	
// 	for (prev_candidate = 0; prev_candidate < first_set_[secondHighestUncoveredPos+1]; prev_candidate++) {

// 	  if (nUncoveredPositions_[prev_candidate] == nPrevSkips) {

// 	    //the candidate set has exactly one entry less
// 	    //now check if the sets match when the highest position is removed from the 

// 	    match = true;
// 	    for (uint k=nMaxSkips-nPrevSkips; k < nMaxSkips; k++) {
// 	      if (uncovered_set_(k-nConsecutiveEndSkips,state) != 
// 		  uncovered_set_(k,prev_candidate)) {
// 		match = false;
// 		break;
// 	      }
// 	    }

// 	    if (match)
// 	      cur_predecessor_sets.push_back(std::make_pair(prev_candidate,highestUncoveredPos+1));
// 	  }
// 	}
// #endif	
      }

// #if 0
//       //b) find states with exactly one entry more
//       for (prev_candidate = 1; prev_candidate < next_set_idx_; prev_candidate++) {

// 	if (nUncoveredPositions_[prev_candidate] == nUncoveredPositions+1) {

// 	  uint nContained = 0;
// 	  uint not_contained_pos = MAX_UINT;
// 	  bool contained;

// 	  uint k,l;

// 	  for (k= nMaxSkips-nUncoveredPositions-1; k < nMaxSkips; k++) {
	    
// 	    const uint entry = uncovered_set_(k,prev_candidate);
	    
// 	    contained = false;
// 	    for (l=nMaxSkips-nUncoveredPositions; l < nMaxSkips; l++) {
// 	      if (entry == uncovered_set_(l,state)) {
// 		contained = true;
// 		break;
// 	      }
// 	    }

// 	    if (contained) {
// 	      nContained++;
// 	    }
// 	    else
// 	      not_contained_pos = entry;
// 	  }
	
// 	  if (nContained == nUncoveredPositions) {
// 	    cur_predecessor_sets.push_back(std::make_pair(prev_candidate,not_contained_pos));
// 	  }
// 	}
//       }
// #endif
    }
    
    const uint nCurPredecessors = cur_predecessor_sets.size();
    predecessor_sets_[state].resize(2,nCurPredecessors);
    uint k;
    for (k=0; k < nCurPredecessors; k++) {
      predecessor_sets_[state](0,k) = cur_predecessor_sets[k].first;
      predecessor_sets_[state](1,k) = cur_predecessor_sets[k].second;
    }

    nMaxPredecessors = std::max(nMaxPredecessors,nCurPredecessors);
  }

  for (uint state = 1; state < nSets; state++) {

    //find successors of the states
    const uint nUncoveredPositions = nUncoveredPositions_[state];
    if (nUncoveredPositions == 1) {

      uint nPrevPredecessors = predecessor_sets_[0].yDim();
      predecessor_sets_[0].resize(2,nPrevPredecessors+1);
      predecessor_sets_[0](0,nPrevPredecessors) = state;
      predecessor_sets_[0](1,nPrevPredecessors) = uncovered_set_(nMaxSkips-1,state);
    }
    else {
      Math1D::NamedVector<uint> cur_uncovered(nUncoveredPositions,MAKENAME(cur_uncovered));
      for (uint k=0; k < nUncoveredPositions; k++) 
	cur_uncovered[k] = uncovered_set_(nMaxSkips-nUncoveredPositions+k,state);
      
      for (uint erase_pos = 0; erase_pos < nUncoveredPositions; erase_pos++) {
	Math1D::NamedVector<uint> succ_uncovered(nUncoveredPositions-1,MAKENAME(succ_uncovered));

	//std::cerr << "A" << std::endl;

	uint l=0;
	for (uint k=0; k < nUncoveredPositions; k++) {
	  if (k != erase_pos) {
	    succ_uncovered[l] = cur_uncovered[k];
	    l++;
	  }
	}

	//std::cerr << "B" << std::endl;

	const uint last_uncovered_pos = succ_uncovered[nUncoveredPositions-2];

	for (uint succ_candidate = first_set_[last_uncovered_pos]; 
	     succ_candidate < first_set_[last_uncovered_pos+1]; succ_candidate++) {

	  if (nUncoveredPositions_[succ_candidate] == nUncoveredPositions-1) {
	    bool match = true;
	    for (uint l=0; l < nUncoveredPositions-1; l++) {

	      if (uncovered_set_(nMaxSkips-nUncoveredPositions+1+l,succ_candidate) != succ_uncovered[l]) {
		match = false;
		break;
	      }
	    }

	    if (match) {

	      uint nPrevPredecessors = predecessor_sets_[succ_candidate].yDim();
	      predecessor_sets_[succ_candidate].resize(2,nPrevPredecessors+1);
	      predecessor_sets_[succ_candidate](0,nPrevPredecessors) = state;
	      predecessor_sets_[succ_candidate](1,nPrevPredecessors) = cur_uncovered[erase_pos];

	      break;
	    }
	  }
	}
      }
    }
  }


  std::cerr << "each state has at most " << nMaxPredecessors << " predecessor states" << std::endl;

  uint nTransitions = 0;
  for (uint s=0; s < nSets; s++)
    nTransitions += predecessor_sets_[s].yDim();

  std::cerr << nTransitions << " transitions" << std::endl;

  //visualize_set_graph("stategraph.dot");
}

void FertilityModelTrainer::visualize_set_graph(std::string filename) {

  std::ofstream dotstream(filename.c_str());
  
  dotstream << "digraph corpus {" << std::endl
	    << "node [fontsize=\"6\",height=\".1\",width=\".1\"];" << std::endl;
  dotstream << "ratio=compress" << std::endl;
  dotstream << "page=\"8.5,11\"" << std::endl;
  
  for (uint state=0; state < uncovered_set_.yDim(); state++) {

    dotstream << "state" << state << " [shape=record,label=\"";
    for (uint k=0; k < uncovered_set_.xDim(); k++) {

      if (uncovered_set_(k,state) == MAX_USHORT)
	dotstream << "-";
      else
	dotstream << uncovered_set_(k,state);

      if (k+1 < uncovered_set_.xDim())
	dotstream << "|";
    }
    dotstream << "\"]" << std::endl;
  }

  for (uint state = 0; state < uncovered_set_.yDim(); state++) {

    for (uint k=0; k < predecessor_sets_[state].yDim(); k++) 
      dotstream << "state" << predecessor_sets_[state](0,k) << " -> state" << state << std::endl; 
  }

  dotstream << "}" << std::endl;
  dotstream.close();
}

void FertilityModelTrainer::compute_coverage_states() {

  const uint nMaxSkips = uncovered_set_.xDim();

  uint nStates = maxJ_+1; //states for set #0
  for (uint k=1; k < uncovered_set_.yDim(); k++) {

    const uint highest_uncovered_pos = uncovered_set_(nMaxSkips-1,k);
    nStates += maxJ_ - highest_uncovered_pos;
  }

  coverage_state_.resize(2,nStates);

  for (uint l=0; l <= maxJ_; l++) {
    coverage_state_(0,l) = 0;
    coverage_state_(1,l) = l;
  }
  
  const uint nUncoveredSets = uncovered_set_.yDim();
  first_state_.resize(maxJ_+2);

  Math2D::NamedMatrix<uint> cov_state_num(uncovered_set_.yDim(),maxJ_+1,MAX_UINT,MAKENAME(cov_state_num));

  uint cur_state = 0;
  for (uint j=0; j <= maxJ_; j++) {

    first_state_[j] = cur_state;
    coverage_state_(0,cur_state) = 0;
    coverage_state_(1,cur_state) = j;
    cov_state_num(0,j) = cur_state;

    cur_state++;
    
    for (uint k=1; k < nUncoveredSets; k++) {
      
      const uint highest_uncovered_pos = uncovered_set_(nMaxSkips-1,k);
      if (highest_uncovered_pos < j) {

 	coverage_state_(0,cur_state) = k;
 	coverage_state_(1,cur_state) = j;
	cov_state_num(k,j) = cur_state;
 	cur_state++;
      }
    }
  }
  first_state_[maxJ_+1] = cur_state;

  std::cerr << nStates << " coverage states" << std::endl;
  
  assert(cur_state == nStates);

  /*** now compute predecessor states ****/
  predecessor_coverage_states_.resize(nStates);

  for (uint state_num = 0; state_num < nStates; state_num++) {

    //std::cerr << "state #" << state_num << std::endl;

    std::vector<std::pair<uint,uint> > cur_predecessor_states;

    const uint highest_covered_source_pos = coverage_state_(1,state_num);
    const uint uncovered_set_idx = coverage_state_(0,state_num);
    const uint highest_uncovered_source_pos = uncovered_set_(nMaxSkips-1,uncovered_set_idx);

    if (highest_uncovered_source_pos == MAX_USHORT) {
      //the set of uncovered positions is empty

      assert(uncovered_set_idx == 0);
      
      if (highest_covered_source_pos > 0) { //otherwise there are no predecessor states
	
	//a) handle transition where the uncovered set is kept
	assert(state_num > 0);
	const uint prev_state = cov_state_num(uncovered_set_idx,highest_covered_source_pos-1);

	assert(coverage_state_(1,prev_state) == highest_covered_source_pos-1);
	assert(coverage_state_(0,prev_state) == uncovered_set_idx);
	cur_predecessor_states.push_back(std::make_pair(prev_state,highest_covered_source_pos));

	//b) handle transitions where the uncovered set is changed
	const uint nPredecessorSets = predecessor_sets_[uncovered_set_idx].yDim();

	for (uint p=0; p < nPredecessorSets; p++) {

	  const uint covered_source_pos = predecessor_sets_[uncovered_set_idx](1,p);
	  if (covered_source_pos < highest_covered_source_pos) {
	    const uint predecessor_set = predecessor_sets_[uncovered_set_idx](0,p);

	    //find the index of the predecessor state
	    const uint prev_idx = cov_state_num(predecessor_set,highest_covered_source_pos);

	    assert(prev_idx < first_state_[highest_covered_source_pos+1]);
	    assert(coverage_state_(1,prev_idx) == highest_covered_source_pos);

	    cur_predecessor_states.push_back(std::make_pair(prev_idx,covered_source_pos));
	  }
	}
      }
      else
	assert(state_num == 0);
    }
    else {
      assert(highest_uncovered_source_pos < highest_covered_source_pos);
      
      //a) handle transition where the uncovered set is kept
      if (highest_covered_source_pos > highest_uncovered_source_pos+1) {

	assert(state_num > 0);
	const uint prev_state = cov_state_num(uncovered_set_idx,highest_covered_source_pos-1);

	assert(coverage_state_(1,prev_state) == highest_covered_source_pos-1);
	assert(coverage_state_(0,prev_state) == uncovered_set_idx);	
	cur_predecessor_states.push_back(std::make_pair(prev_state,highest_covered_source_pos));	
      }

      //b) handle transitions where the uncovered set is changed
      const uint nPredecessorSets = predecessor_sets_[uncovered_set_idx].yDim();
      
//       std::cerr << "examining state (";
//       print_uncovered_set(uncovered_set_idx);
//       std::cerr << " ; " << highest_covered_source_pos << " )" << std::endl;

      for (uint p=0; p < nPredecessorSets; p++) {
	
	const uint covered_source_pos = predecessor_sets_[uncovered_set_idx](1,p);
	if (covered_source_pos <= highest_covered_source_pos) {
	  const uint predecessor_set = predecessor_sets_[uncovered_set_idx](0,p);

// 	  std::cerr << "predecessor set ";
// 	  print_uncovered_set(predecessor_set);
// 	  std::cerr << std::endl;
	  
	  uint prev_highest_covered = highest_covered_source_pos;
	  if (covered_source_pos == highest_covered_source_pos) {
	    if (nUncoveredPositions_[predecessor_set] < nUncoveredPositions_[uncovered_set_idx])
	      prev_highest_covered = j_before_end_skips_[uncovered_set_idx];
	    else
	      //in this case there is no valid transition
 	      prev_highest_covered = MAX_UINT;
	  }
	  else if (nUncoveredPositions_[predecessor_set] < nUncoveredPositions_[uncovered_set_idx]) {
	    //if a new position is skipped, the highest covered source pos. must be the one after the skip
	    prev_highest_covered = MAX_UINT;
	  }

	  if (prev_highest_covered != MAX_UINT) {
// 	    std::cerr << "prev_highest_covered: " << prev_highest_covered << std::endl;
	    
	    //find the index of the predecessor state
	    const uint prev_idx = cov_state_num(predecessor_set,prev_highest_covered);

	    assert(prev_idx < first_state_[prev_highest_covered+1]);
	    assert(coverage_state_(1,prev_idx) == prev_highest_covered);
	    
	    cur_predecessor_states.push_back(std::make_pair(prev_idx,covered_source_pos));
	  }
	}
      }
    }
    
    /*** copy cur_predecessor_states to predecessor_covered_sets_[state_num] ***/
    predecessor_coverage_states_[state_num].resize(2,cur_predecessor_states.size());
    for (uint k=0; k < cur_predecessor_states.size(); k++) {
      predecessor_coverage_states_[state_num](0,k) = cur_predecessor_states[k].first;
      predecessor_coverage_states_[state_num](1,k) = cur_predecessor_states[k].second;
    }
    
  }
}

void FertilityModelTrainer::write_alignments(const std::string filename) const {

  std::ofstream out(filename.c_str());

  for (uint s=0; s < source_sentence_.size(); s++) {

    const uint curJ = source_sentence_[s].size();

    for (uint j=0; j < curJ; j++)
      out << best_known_alignment_[s][j] << " ";
    out << std::endl;
  }

  out.close();
}

/************************** implementation of IBM3Trainer *********************/

IBM3Trainer::IBM3Trainer(const Storage1D<Storage1D<uint> >& source_sentence,
			 const Storage1D<Math2D::Matrix<uint> >& slookup,
			 const Storage1D<Storage1D<uint> >& target_sentence,
			 const std::map<uint,std::set<std::pair<uint,uint> > >& sure_ref_alignments,
			 const std::map<uint,std::set<std::pair<uint,uint> > >& possible_ref_alignments,
			 SingleWordDictionary& dict,
			 const CooccuringWordsType& wcooc,
			 uint nSourceWords, uint nTargetWords,
			 const floatSingleWordDictionary& prior_weight,
			 bool parametric_distortion, bool och_ney_empty_word, bool viterbi_ilp, double l0_fertpen)
  : FertilityModelTrainer(source_sentence,slookup,target_sentence,dict,wcooc,
			  nSourceWords,nTargetWords,sure_ref_alignments,possible_ref_alignments),
    distortion_prob_(MAKENAME(distortion_prob_)), och_ney_empty_word_(och_ney_empty_word), prior_weight_(prior_weight),
    l0_fertpen_(l0_fertpen), parametric_distortion_(parametric_distortion), viterbi_ilp_(viterbi_ilp) {

#ifndef HAS_CBC
  viterbi_ilp_ = false;
#endif

  uint maxI = 0;

  p_zero_ = 0.1;
  p_nonzero_ = 0.9;

  for (uint s=0; s < source_sentence_.size(); s++) {

    const uint curI = target_sentence_[s].size();

    if (maxI < curI)
      maxI = curI;
  }
  
  distortion_prob_.resize(maxJ_);
  if (parametric_distortion_)
    distortion_param_.resize(maxJ_,maxI_,1.0);

  Math1D::Vector<uint> max_I(maxJ_,0);

  for (uint s=0; s < source_sentence_.size(); s++) {

    const uint curI = target_sentence_[s].size();
    const uint curJ = source_sentence_[s].size();

    if (curI > max_I[curJ-1])
      max_I[curJ-1] = curI;
  }

  for (uint J=0; J < maxJ_; J++) {
    distortion_prob_[J].resize_dirty(J+1,max_I[J]);
    distortion_prob_[J].set_constant(1.0 / (J+1));
  }
}

double IBM3Trainer::p_zero() const {
  return p_zero_;
}

void IBM3Trainer::init_from_hmm(const FullHMMAlignmentModel& align_model,
				const InitialAlignmentProbability& initial_prob) {

  std::cerr << "initializing IBM3 from HMM" << std::endl;

  NamedStorage1D<Math1D::Vector<uint> > fert_count(nTargetWords_,MAKENAME(fert_count));
  for (uint i=0; i < nTargetWords_; i++) {
    fert_count[i].resize(fertility_prob_[i].size(),0);
  }

  for (uint s=0; s < source_sentence_.size(); s++) {

    const uint curI = target_sentence_[s].size();
    const uint curJ = source_sentence_[s].size();

    Math1D::Vector<uint> uint_alignment(curJ);

    if (initial_prob.size() == 0) 
      compute_fullhmm_viterbi_alignment(source_sentence_[s],slookup_[s], target_sentence_[s], 
					dict_, align_model[curI-1], uint_alignment);
    else {
      compute_ehmm_viterbi_alignment(source_sentence_[s],slookup_[s], target_sentence_[s], 
				     dict_, align_model[curI-1], initial_prob[curI-1],
				     uint_alignment);
    }

    best_known_alignment_[s].resize(curJ);
    for (uint j=0; j < curJ; j++)
      best_known_alignment_[s][j] = uint_alignment[j];

    Math1D::NamedVector<uint> fertility(curI+1,0,MAKENAME(fertility));

    for (uint j=0; j < curJ; j++) {
      const uint aj = best_known_alignment_[s][j];
      fertility[aj]++;
    }

    if (2*fertility[0] > curJ) {

      std::cerr << "fixing sentence pair #" << s << std::endl;

      for (uint j=0; j < curJ; j++) {

	if (best_known_alignment_[s][j] == 0) {

	  best_known_alignment_[s][j] = 1;
	  fertility[0]--;
	  fertility[1]++;

	  if (dict_[target_sentence_[s][0]][slookup_[s](j,0)] < 0.001) {

	    dict_[target_sentence_[s][0]] *= 0.99;
	    dict_[target_sentence_[s][0]][slookup_[s](j,0)] += 0.01;
	  } 
	}
      }
    }

    fert_count[0][fertility[0]]++;
    
    for (uint i=0; i < curI; i++) {
      const uint t_idx = target_sentence_[s][i];

      fert_count[t_idx][fertility[i+1]]++;
    }
  }

  //init fertility prob. by weighted combination of uniform distribution 
  // and counts from Viterbi alignments
  
  //const double count_weight = 0.75;
  const double count_weight = 0.95;

  const double uni_weight = 1.0 - count_weight;
  for (uint i=0; i < nTargetWords_; i++) {

    double inv_fc_sum = 1.0 / fert_count[i].sum();
    
    const uint max_fert = fert_count[i].size();
    double uni_contrib = uni_weight / max_fert;
    for (uint f=0; f < max_fert; f++) {

      fertility_prob_[i][f] = uni_contrib + count_weight * inv_fc_sum * fert_count[i][f];
    }
  }

  std::cerr << "initializing distortion prob. by forward-backward HMM" << std::endl;

  ReducedIBM3DistortionModel fdcount(distortion_prob_.size(),MAKENAME(fdcount));
  for (uint J=0; J < distortion_prob_.size(); J++) {
    fdcount[J].resize(J+1,distortion_prob_[J].yDim(),0.0);
  }

  p_zero_ = 0.0;
  p_nonzero_ = 0.0;

  Math1D::Vector<double> empty_vec;

  //init distortion probabilities using forward-backward
  for (uint s=0; s < source_sentence_.size(); s++) {

    const Storage1D<uint>& cur_source = source_sentence_[s];
    const Storage1D<uint>& cur_target = target_sentence_[s];
    const Math2D::Matrix<uint>& cur_lookup = slookup_[s];
    
    const uint curJ = cur_source.size();
    const uint curI = cur_target.size();

    Math2D::NamedMatrix<long double> forward(2*curI,curJ,MAKENAME(forward));
    Math2D::NamedMatrix<long double> backward(2*curI,curJ,MAKENAME(backward));

    
    const Math1D::Vector<double> init_prob = (initial_prob.size() > 0) ? initial_prob[curI-1] : empty_vec;

    calculate_hmm_forward(cur_source, cur_target, cur_lookup, dict_,
			  align_model[curI-1], init_prob, forward);
    
    calculate_hmm_backward(cur_source, cur_target, cur_lookup, dict_,
			   align_model[curI-1], init_prob, backward, false);

    // extract fractional counts
    long double sentence_prob = 0.0;
    for (uint i=0; i < 2*curI; i++)
      sentence_prob += forward(i,curJ-1);
    long double inv_sentence_prob = 1.0 / sentence_prob;

    assert(!isnan(inv_sentence_prob));

    for (uint i=0; i < curI; i++) {
      const uint t_idx = cur_target[i];


      for (uint j=0; j < curJ; j++) {
	
	if (dict_[t_idx][cur_lookup(j,i)] > 0.0) {
	  double contrib = inv_sentence_prob * forward(i,j) * backward(i,j) 
	    / dict_[t_idx][cur_lookup(j,i)];
	  
	  fdcount[curJ-1](j,i) += contrib;
	  p_nonzero_ += contrib;
	}
      }
    }
    for (uint i=curI; i < 2*curI; i++) {
      for (uint j=0; j < curJ; j++) {
	const uint s_idx = cur_source[j];
	
	if (dict_[0][s_idx-1] > 0.0) {
	  double contrib = inv_sentence_prob * forward(i,j) * backward(i,j) 
	    / dict_[0][s_idx-1];
	  
	  p_zero_ += contrib;
	}
	
      }
    }

  }

  double p_norm = p_zero_ + p_nonzero_;
  assert(p_norm != 0.0);
  p_zero_ /= p_norm;
  p_nonzero_ /= p_norm;

  std::cerr << "initial value of p_zero: " << p_zero_ << std::endl;

  for (uint J=0; J < distortion_prob_.size(); J++) {

    for (uint i=0; i < distortion_prob_[J].yDim(); i++) {

      double sum = 0.0;
      for (uint j=0; j < J+1; j++)
	sum += fdcount[J](j,i);
      
      assert(!isnan(sum));
      
      if (sum > 1e-305) {
	double inv_sum = 1.0 / sum;

	assert(!isnan(inv_sum));

	for (uint j=0; j < J+1; j++)
	  distortion_prob_[J](j,i) = inv_sum * fdcount[J](j,i);
      }
    }
  }
}

void IBM3Trainer::par2nonpar_distortion() {

  for (uint J=0; J < maxJ_; J++) {

    for (uint i=0; i < distortion_prob_[J].yDim(); i++) {

      double sum = 0.0;
      for (uint j=0; j < J+1; j++)
	sum += distortion_param_(j,i);
      
      assert(!isnan(sum));
      
      if (sum > 1e-305) {
	double inv_sum = 1.0 / sum;

	assert(!isnan(inv_sum));

	for (uint j=0; j < J+1; j++)
	  distortion_prob_[J](j,i) = inv_sum * distortion_param_(j,i);
      }
    }
  }
}

double IBM3Trainer::par_distortion_m_step_energy(const ReducedIBM3DistortionModel& fdistort_count,
						 const Math2D::Matrix<double>& param) {

  double energy = 0.0;

  for (uint J=0; J < maxJ_; J++) {

    const Math2D::Matrix<double>& cur_distort_count = fdistort_count[J];

    if (cur_distort_count.size() > 0) {

      for (uint j=0; j < cur_distort_count.xDim(); j++) {
      
	double sum = 0.0;
	for (uint i=0; i < cur_distort_count.yDim(); i++) 
	  sum += param(j,i);

	for (uint i=0; i < cur_distort_count.yDim(); i++) 
	  energy -= cur_distort_count(j,i) * std::log( param(j,i) / sum);
      }
    }
  }

  return energy;
}

void IBM3Trainer::par_distortion_m_step(const ReducedIBM3DistortionModel& fdistort_count) {

  double alpha = 0.1;

  double energy = par_distortion_m_step_energy(fdistort_count, distortion_param_);

  Math2D::Matrix<double> distortion_grad(maxJ_,maxI_,0.0);
  Math2D::Matrix<double> new_distortion_param(maxJ_,maxI_,0.0);
  Math2D::Matrix<double> hyp_distortion_param(maxJ_,maxI_,0.0);

  double line_reduction_factor = 0.35;

  for (uint iter = 1; iter <= 400; iter++) {

    if ((iter % 5) == 0)
      std::cerr << "m-step iter # " << iter << ", energy: " << energy << std::endl;

    distortion_grad.set_constant(0.0);

    /*** compute gradient ***/
    for (uint J=0; J < maxJ_; J++) {

      const Math2D::Matrix<double>& cur_distort_count = fdistort_count[J];

      if (cur_distort_count.size() > 0) {
	
	for (uint j=0; j < cur_distort_count.xDim(); j++) {
	  
	  double sum = 0.0;
	  for (uint i=0; i < cur_distort_count.yDim(); i++) 
	    sum += distortion_param_(j,i);

	  double count_sum = 0.0;
	  for (uint i=0; i < cur_distort_count.yDim(); i++) {
	    count_sum += cur_distort_count(j,i);

	    double cur_param = std::max(1e-15,distortion_param_(j,i));
	  
	    distortion_grad(j,i) -= cur_distort_count(j,i) / cur_param;
	  }

	  for (uint i=0; i < cur_distort_count.yDim(); i++) {
	    distortion_grad(j,i) += count_sum / sum;
	  }
	}
      }
    }

    /*** go on neg. gradient direction and reproject ***/
    for (uint j=0; j < distortion_param_.xDim(); j++) {
      
      Math1D::Vector<double> temp(maxI_);

      for (uint i=0; i < maxI_; i++)
	temp[i] = distortion_param_(j,i) - alpha * distortion_grad(j,i) ;

      projection_on_simplex(temp.direct_access(), maxI_);

      for (uint i=0; i < maxI_; i++)
	new_distortion_param(j,i) = temp[i];
    }

    /*** find appropriate step-size ***/

    double best_lambda = 1.0;
    double lambda = 1.0;

    double best_energy = 1e300;

    uint nIter = 0;

    bool decreasing = false;

    while (decreasing || best_energy > energy) {

      nIter++;

      lambda *= line_reduction_factor;
      double neg_lambda = 1.0 - lambda;

      for (uint k=0; k < hyp_distortion_param.size(); k++)
	hyp_distortion_param.direct_access(k) = lambda * new_distortion_param.direct_access(k) 
	  + neg_lambda * distortion_param_.direct_access(k);

      double hyp_energy = par_distortion_m_step_energy(fdistort_count, hyp_distortion_param);

      if (hyp_energy < best_energy) {

	best_energy = hyp_energy;
	best_lambda = lambda;
	decreasing = true;
      }
      else
	decreasing = false;

      if (nIter > 5 && best_energy < 0.975 * energy)
	break;
    }
    //std::cerr << "best lambda: " << best_lambda << std::endl;

    if (nIter > 6)
      line_reduction_factor *= 0.9;

    double neg_best_lambda = 1.0 - best_lambda;

    for (uint k=0; k < hyp_distortion_param.size(); k++)
      distortion_param_.direct_access(k) = best_lambda * new_distortion_param.direct_access(k) 
	+ neg_best_lambda * distortion_param_.direct_access(k);

    energy = best_energy;

  }

}

long double IBM3Trainer::alignment_prob(uint s, const Math1D::Vector<ushort>& alignment) const {

  long double prob = 1.0;

  const Storage1D<uint>& cur_source = source_sentence_[s];
  const Storage1D<uint>& cur_target = target_sentence_[s];
  const Math2D::Matrix<uint>& cur_lookup = slookup_[s];

  const uint curI = target_sentence_[s].size();
  const uint curJ = source_sentence_[s].size();

  const Math2D::Matrix<double>& cur_distort_prob = distortion_prob_[curJ-1];

  assert(alignment.size() == curJ);

  Math1D::NamedVector<uint> fertility(curI+1,0,MAKENAME(fertility));
  
  for (uint j=0; j < curJ; j++) {
    const uint aj = alignment[j];
    fertility[aj]++;
  }

  //std::cerr << "ap: fertility: " << fertility << std::endl;
  
  if (curJ < 2*fertility[0])
    return 0.0;

  for (uint i=1; i <= curI; i++) {
    uint t_idx = cur_target[i-1];
    prob *= ldfac(fertility[i]) * fertility_prob_[t_idx][fertility[i]];
//     std::cerr << "fertility_factor(" << i << "): " 
// 	      << (ldfac(fertility[i]) * fertility_prob_[t_idx][fertility[i]])
// 	      << std::endl;
  }
  for (uint j=0; j < curJ; j++) {
    
    uint s_idx = cur_source[j];
    uint aj = alignment[j];
    
    if (aj == 0)
      prob *= dict_[0][s_idx-1];
    else {
      uint t_idx = cur_target[aj-1];
      prob *= dict_[t_idx][cur_lookup(j,aj-1)] * cur_distort_prob(j,aj-1);
//       std::cerr << "dict-factor(" << j << "): "
// 		<< dict_[t_idx][cur_lookup(j,aj-1)] << std::endl;
//       std::cerr << "distort-factor(" << j << "): "
// 		<< cur_distort_prob(j,aj-1) << std::endl;
    }
  }

  //std::cerr << "ap before empty word: " << prob << std::endl;

  //handle empty word
  assert(fertility[0] <= 2*curJ);
  
  prob *= ldchoose(curJ-fertility[0],fertility[0]);
  for (uint k=1; k <= fertility[0]; k++)
    prob *= p_zero_;
  for (uint k=1; k <= curJ-2*fertility[0]; k++)
    prob *= p_nonzero_;

  if (och_ney_empty_word_) {

    for (uint k=1; k<= fertility[0]; k++)
      prob *= ((long double) k) / curJ;
  }

  return prob;
}


long double IBM3Trainer::update_alignment_by_hillclimbing(uint s, uint& nIter, Math1D::Vector<uint>& fertility,
							  Math2D::Matrix<long double>& expansion_prob,
							  Math2D::Matrix<long double>& swap_prob) {

  //std::cerr << "*************** hillclimb(" << s << ")" << std::endl;
  //std::cerr << "start alignment: " << best_known_alignment_[s] << std::endl;

  const Storage1D<uint>& cur_source = source_sentence_[s];
  const Storage1D<uint>& cur_target = target_sentence_[s];
  const Math2D::Matrix<uint>& cur_lookup = slookup_[s];
  
  const uint curI = target_sentence_[s].size();
  const uint curJ = source_sentence_[s].size();

  const Math2D::Matrix<double>& cur_distort_prob = distortion_prob_[curJ-1];

  /**** calculate probability of so far best known alignment *****/
  long double base_prob = 1.0;
  
  fertility.set_constant(0);

  for (uint j=0; j < curJ; j++) {
    const uint aj = best_known_alignment_[s][j];
    fertility[aj]++;
  
    //handle lexicon prob and distortion prob for aj != 0
    if (aj == 0) {
      base_prob *= dict_[0][cur_source[j]-1];
    }
    else {
      base_prob *= cur_distort_prob(j,aj-1);
      base_prob *= dict_[cur_target[aj-1]][cur_lookup(j,aj-1)];
    }
  }

  assert(2*fertility[0] <= curJ);
  assert(!isnan(base_prob));

  //handle fertility prob
  for (uint i=0; i < curI; i++) {
      
    const uint fert = fertility[i+1];
    const uint t_idx = cur_target[i];

    if (!(fertility_prob_[t_idx][fert] > 0)) {

      std::cerr << "fert_prob[" << t_idx << "][" << fert << "]: " << fertility_prob_[t_idx][fert] << std::endl;
      std::cerr << "target sentence #" << s << ", word #" << i << std::endl;
      std::cerr << "alignment: " << best_known_alignment_[s] << std::endl;
    }

    assert(fertility_prob_[t_idx][fert] > 0);

    base_prob *= ldfac(fert) * fertility_prob_[t_idx][fert];
  }
  
  assert(!isnan(base_prob));

  //std::cerr << "base prob before empty word: " << base_prob << std::endl;

  //handle fertility of empty word
  uint zero_fert = fertility[0];
  if (curJ < 2*zero_fert) {
    std::cerr << "WARNING: alignment startpoint for HC violates the assumption that less words "
	      << " are aligned to NULL than to a real word" << std::endl;
  }
  else {

    assert(zero_fert <= 15);
    base_prob *= ldchoose(curJ-zero_fert,zero_fert);
    for (uint k=1; k <= zero_fert; k++)
      base_prob *= p_zero_;
    for (uint k=1; k <= curJ-2*zero_fert; k++) {
      base_prob *= p_nonzero_;
    }

    if (och_ney_empty_word_) {

      for (uint k=1; k<= fertility[0]; k++)
	base_prob *= ((long double) k) / curJ;
    }
  }

  // long double check = alignment_prob(s,best_known_alignment_[s]);

  // long double check_ratio = base_prob / check;

  // if (!(check_ratio > 0.999 && check_ratio < 1.001)) {
  //   std::cerr << "alignment: " << best_known_alignment_[s] << std::endl;
  //   std::cerr << "fertility: " << fertility << std::endl;
  //   std::cerr << "base_prob: " << base_prob << std::endl;
  //   std::cerr << "check: " << check << std::endl;
  
  //   std::cerr << "ratio: " << check_ratio << std::endl;
  // }

  // if (base_prob > 1e-300 || check > 1e-300)
  //   assert(check_ratio > 0.999 && check_ratio < 1.001);

  assert(!isnan(base_prob));


  swap_prob.resize(curJ,curJ);
  expansion_prob.resize(curJ,curI+1);


  //DEBUG
  uint count_iter = 0;
  //END_DEBUG

  while (true) {    

    //DEBUG
    count_iter++;
    //END_DEBUG
    nIter++;
    //std::cerr << "****************** starting new hillclimb iteration, current best prob: " << base_prob << std::endl;

    bool improvement = false;

    double best_prob = base_prob;
    bool best_change_is_move = false;
    uint best_move_j = MAX_UINT;
    uint best_move_aj = MAX_UINT;
    uint best_swap_j1 = MAX_UINT;
    uint best_swap_j2 = MAX_UINT;

    /**** scan neighboring alignments and keep track of the best one that is better 
     ****  than the current alignment  ****/

    Math1D::Vector<long double> fert_increase_factor(curI+1);
    Math1D::Vector<long double> fert_decrease_factor(curI+1);

    for (uint i=1; i <= curI; i++) {

      uint t_idx = cur_target[i-1];
      uint cur_fert = fertility[i];

      assert(fertility_prob_[t_idx][cur_fert] > 0.0);

      if (cur_fert > 0) {
	fert_decrease_factor[i] = ((long double) fertility_prob_[t_idx][cur_fert-1]) 
	  / (cur_fert * fertility_prob_[t_idx][cur_fert]);
      }
      else
	fert_decrease_factor[i] = 0.0;

      if (cur_fert+1 < fertility_prob_[t_idx].size())
	fert_increase_factor[i] = ((long double) (cur_fert+1) * fertility_prob_[t_idx][cur_fert+1])
	  / fertility_prob_[t_idx][cur_fert];
      else
	fert_increase_factor[i] = 0.0;
    }
    
    //a) expansion moves

    //std::cerr << "considering moves" << std::endl;

    long double empty_word_increase_const = 0.0;
    if (curJ >= 2*(zero_fert+1)) {
      empty_word_increase_const = ldchoose(curJ-zero_fert-1,zero_fert+1) * p_zero_ 
	/ (ldchoose(curJ-zero_fert,zero_fert) * p_nonzero_ * p_nonzero_);

      if (och_ney_empty_word_) {
	empty_word_increase_const *= (zero_fert+1) / ((long double) curJ);
      }
    }
    else {
      if (curJ > 3) {
	std::cerr << "WARNING: reached limit of allowed number of zero-aligned words, " 
		  << "J=" << curJ << ", zero_fert =" << zero_fert << std::endl;
      }
    }

    long double empty_word_decrease_const = 0.0;
    if (zero_fert > 0) {
      empty_word_decrease_const = ldchoose(curJ-zero_fert+1,zero_fert-1) * p_nonzero_ * p_nonzero_ 
	/ (ldchoose(curJ-zero_fert,zero_fert) * p_zero_);

      if (och_ney_empty_word_) {
	empty_word_decrease_const *= curJ / ((long double) zero_fert);
      }
    }

    for (uint j=0; j < curJ; j++) {

      const uint s_idx = cur_source[j];

      const uint aj = best_known_alignment_[s][j];
      assert(fertility[aj] > 0);
      expansion_prob(j,aj) = 0.0;
      
      //std::cerr << "j: " << j << ", aj: " << aj << std::endl;

      long double mod_base_prob = base_prob;

      if (aj > 0) {
	const uint t_idx = cur_target[aj-1];

	if (dict_[t_idx][cur_lookup(j,aj-1)] * cur_distort_prob(j,aj-1) > 1e-305) {
	  mod_base_prob *= fert_decrease_factor[aj] / dict_[t_idx][cur_lookup(j,aj-1)];	
	  mod_base_prob /= cur_distort_prob(j,aj-1);
	}
	else
	  mod_base_prob *= 0.0;
      }
      else {
	if (dict_[0][s_idx-1] > 1e-305)
	  mod_base_prob *= empty_word_decrease_const / dict_[0][s_idx-1];
	else
	  mod_base_prob *= 0.0;
      }

      assert(!isnan(mod_base_prob));
      
      for (uint cand_aj = 0; cand_aj <= curI; cand_aj++) {

// 	std::cerr << "examining move " << j << " -> " << cand_aj << " (instead of " << aj << ") in iteration #" 
// 		  << count_iter << std::endl;
// 	std::cerr << "cand_aj has then fertility " << (fertility[cand_aj]+1) << std::endl;
// 	std::cerr << "current aj reduces its fertility from " << fertility[aj] << " to " << (fertility[aj]-1)
// 		  << std::endl;

// 	if (cand_aj != 0 && cand_aj != aj) {
// 	  std::cerr << "previous fert prob of candidate: " 
// 		    << fertility_prob_[cur_target[cand_aj-1]][fertility[cand_aj]] << std::endl;
// 	  std::cerr << "new fert prob of candidate: " 
// 		    << fertility_prob_[cur_target[cand_aj-1]][fertility[cand_aj]+1] << std::endl;
// 	}
// 	if (aj != 0) {
// 	  std::cerr << "previous fert. prob of aj: " << fertility_prob_[cur_target[aj-1]][fertility[aj]] << std::endl;
// 	  std::cerr << "new fert. prob of aj: " << fertility_prob_[cur_target[aj-1]][fertility[aj]-1] << std::endl;
// 	}

// 	if (aj != 0) 
// 	  std::cerr << "prev distort prob: " << cur_distort_prob(j,aj-1) << std::endl;
// 	if (cand_aj != 0)
// 	  std::cerr << "new distort prob: " << cur_distort_prob(j,cand_aj-1) << std::endl;
	
	if (cand_aj != aj) {
	  
	  long double hyp_prob = mod_base_prob;

	  if (cand_aj != 0) {

	    const uint t_idx = cur_target[cand_aj-1];

	    hyp_prob *= dict_[t_idx][cur_lookup(j,cand_aj-1)];
	    hyp_prob *= fert_increase_factor[cand_aj];
	    hyp_prob *= cur_distort_prob(j,cand_aj-1);
	  }
	  else {
	    hyp_prob *= empty_word_increase_const * dict_[0][s_idx-1];
	  }

	  //std::cerr << "hyp_prob: " << hyp_prob << std::endl;

	  if (isnan(hyp_prob)) {
	    INTERNAL_ERROR << " nan in move " << j << " -> " << cand_aj << " . Exiting." << std::endl;

	    std::cerr << "mod_base_prob: " << mod_base_prob << std::endl;
	    if (cand_aj != 0) {

	      const uint t_idx = cur_target[cand_aj-1];

	      std::cerr << "dict-factor: " << dict_[t_idx][cur_lookup(j,cand_aj-1)] << std::endl;
	      std::cerr << "fert-factor: " << fert_increase_factor[cand_aj] << std::endl;
	      std::cerr << "distort-factor: " << cur_distort_prob(j,cand_aj-1) << std::endl;

	      std::cerr << "distort table: " << cur_distort_prob << std::endl;
	    }
	    exit(1);
	  }

	  assert(!isnan(hyp_prob));

	  //DEBUG
// 	  Math1D::Vector<uint> cand_alignment = best_known_alignment_[s];
// 	  cand_alignment[j] = cand_aj;
	
// 	  if (hyp_prob > 0.0) {
// 	    double check_ratio = hyp_prob / alignment_prob(s,cand_alignment);
// 	    if (hyp_prob > best_prob* 0.0000001) {

// 	      if (! (check_ratio > 0.999 && check_ratio < 1.001 ) ) {
// 		std::cerr << "check for sentence pair #" << s << " with " << curJ << " source words and "
// 			  << curI << " target words" << std::endl;

// 		std::cerr << "j: " << j << ", aj: " << aj << std::endl;
		
// 		std::cerr << "examining move " << j << " -> " << cand_aj << " (instead of " << aj << ") in iteration #" 
// 			  << count_iter << std::endl;
// 		std::cerr << "cand_aj has then fertility " << (fertility[cand_aj]+1) << std::endl;
// 		std::cerr << "current aj reduces its fertility from " << fertility[aj] << " to " << (fertility[aj]-1)
// 			  << std::endl;
		
// 		if (cand_aj != 0 && cand_aj != aj) {
// 		  std::cerr << "previous fert prob of candidate: " 
// 			    << fertility_prob_[cur_target[cand_aj-1]][fertility[cand_aj]] << std::endl;
// 		  std::cerr << "new fert prob of candidate: " 
// 			    << fertility_prob_[cur_target[cand_aj-1]][fertility[cand_aj]+1] << std::endl;
// 		}
// 		if (aj != 0) {
// 		  std::cerr << "previous fert. prob of aj: " << fertility_prob_[cur_target[aj-1]][fertility[aj]] 
// 			    << std::endl;
// 		  std::cerr << "new fert. prob of aj: " << fertility_prob_[cur_target[aj-1]][fertility[aj]-1] 
// 			    << std::endl;
// 		}

// 		if (aj != 0) 
// 		  std::cerr << "prev distort prob: " << cur_distort_prob(j,aj-1) << std::endl;
// 		if (cand_aj != 0)
// 		  std::cerr << "new distort prob: " << cur_distort_prob(j,cand_aj-1) << std::endl;

// 		std::cerr << "hyp prob: " << hyp_prob << std::endl;
// 		std::cerr << "check: " << alignment_prob(s,cand_alignment) << std::endl;
// 		std::cerr << "best prob: " << best_prob << std::endl;
// 	      }
	      
// 	      assert(check_ratio > 0.999 && check_ratio < 1.001);
// 	    }
// 	    else if (check_ratio < 0.999 || check_ratio > 1.001) {
// #if 0
// 	      std::cerr << "WARNING: ratio " << check_ratio << " for very unlikely alignment" << std::endl; 
// 	      std::cerr << "   hyp prob: " << hyp_prob << std::endl;
// 	      std::cerr << "   check: " << alignment_prob(s,cand_alignment) << std::endl;
// 	      std::cerr << "   best prob: " << best_prob << std::endl;
// #endif
// 	    }
// 	  }
	  //END_DEBUG

	  expansion_prob(j,cand_aj) = hyp_prob;
	  
	  if (hyp_prob > 1.0000001*best_prob) {
	    //std::cerr << "improvement of " << (hyp_prob - best_prob) << std::endl;

	    best_prob = hyp_prob;
	    improvement = true;
	    best_change_is_move = true;
	    best_move_j = j;
	    best_move_aj = cand_aj;
	  }	  
	}
      }
    }

//     if (improvement) {
//       std::cerr << "expansion improvement for sentence pair #" << s << std::endl;
//     }

    //std::cerr << "improvements for moves: " << improvement << std::endl;
    //std::cerr << "considering swaps" << std::endl;

    //b) swap_moves (NOTE that swaps do not affect the fertilities)
    for (uint j1=0; j1 < curJ; j1++) {

      swap_prob(j1,j1) = 0.0;
      //std::cerr << "j1: " << j1 << std::endl;
      
      const uint aj1 = best_known_alignment_[s][j1];
      const uint s_j1 = cur_source[j1];

      for (uint j2 = j1+1; j2 < curJ; j2++) {

	//std::cerr << "j2: " << j2 << std::endl;

	const uint aj2 = best_known_alignment_[s][j2];
	const uint s_j2 = cur_source[j2];

	if (aj1 == aj2) {
	  //we do not want to count the same alignment twice
	  swap_prob(j1,j2) = 0.0;
	}
	else {

	  long double hyp_prob = base_prob;
	  if (aj1 != 0) {
	    const uint t_idx = cur_target[aj1-1];
	    hyp_prob *= dict_[t_idx][cur_lookup(j2,aj1-1)] * cur_distort_prob(j2,aj1-1)
	      / (dict_[t_idx][cur_lookup(j1,aj1-1)] * cur_distort_prob(j1,aj1-1) ) ;
	  }
	  else
	    hyp_prob *= dict_[0][s_j2-1] / dict_[0][s_j1-1];

	  if (aj2 != 0) {
	    const uint t_idx = cur_target[aj2-1];
	    hyp_prob *= dict_[t_idx][cur_lookup(j1,aj2-1)] * cur_distort_prob(j1,aj2-1)
	      / (dict_[t_idx][cur_lookup(j2,aj2-1)] * cur_distort_prob(j2,aj2-1) );
	  }
	  else {
	    hyp_prob *= dict_[0][s_j1-1] / dict_[0][s_j2-1];
	  }

	  //DEBUG
// 	  Math1D::Vector<uint> cand_alignment = best_known_alignment_[s];
// 	  cand_alignment[j1] = aj2;
// 	  cand_alignment[j2] = aj1;
	  
// 	  double check_ratio = hyp_prob / alignment_prob(s,cand_alignment);
// 	  if (hyp_prob > best_prob* 0.0000001) 
// 	    assert(check_ratio > 0.999 && check_ratio < 1.001);
	  //END_DEBUG

	  assert(!isnan(hyp_prob));

	  swap_prob(j1,j2) = hyp_prob;

	  if (hyp_prob > 1.0000001*best_prob) {
	    
	    improvement = true;
	    best_change_is_move = false;
	    best_prob = hyp_prob;
	    best_swap_j1 = j1;
	    best_swap_j2 = j2;
	  }
	}
	swap_prob(j2,j1) = swap_prob(j1,j2);
      }
    }
    
    //std::cerr << "[s=" << s << "] swaps done, improvement: " << improvement << std::endl;

    if (!improvement)
      break;

    //update alignment
    if (best_change_is_move) {
      uint cur_aj = best_known_alignment_[s][best_move_j];
      assert(cur_aj != best_move_aj);

      //std::cerr << "moving source pos" << best_move_j << " from " << cur_aj << " to " << best_move_aj << std::endl;

      best_known_alignment_[s][best_move_j] = best_move_aj;
      fertility[cur_aj]--;
      fertility[best_move_aj]++;
      zero_fert = fertility[0];
    }
    else {

      uint cur_aj1 = best_known_alignment_[s][best_swap_j1];
      uint cur_aj2 = best_known_alignment_[s][best_swap_j2];

      assert(cur_aj1 != cur_aj2);
      
      best_known_alignment_[s][best_swap_j1] = cur_aj2;
      best_known_alignment_[s][best_swap_j2] = cur_aj1;
    }

    //std::cerr << "probability improved from " << base_prob << " to " << best_prob << std::endl;

    // double check_ratio = best_prob / alignment_prob(s,best_known_alignment_[s]);
    // //std::cerr << "check_ratio: " << check_ratio << std::endl;
    // if (best_prob > 1e-300)
    //   assert(check_ratio > 0.999 && check_ratio < 1.001);

    base_prob = best_prob;
  }
  
  //std::cerr << "leaving hillclimb" << std::endl;

  assert(!isnan(base_prob));

  assert(2*fertility[0] <= curJ);

  return base_prob;
}


void IBM3Trainer::update_alignments_unconstrained() {

  Math2D::NamedMatrix<long double> expansion_prob(MAKENAME(expansion_prob));
  Math2D::NamedMatrix<long double> swap_prob(MAKENAME(swap_prob));

  for (uint s=0; s < source_sentence_.size(); s++) {

    const uint curI = target_sentence_[s].size();
    Math1D::NamedVector<uint> fertility(curI+1,0,MAKENAME(fertility));
    
    uint nIter=0;
    update_alignment_by_hillclimbing(s,nIter,fertility,expansion_prob,swap_prob);
  }
}


void IBM3Trainer::train_unconstrained(uint nIter) {

  std::cerr << "starting IBM3 training without constraints" << std::endl;

  double max_perplexity = 0.0;
  double viterbi_max_perplexity = 0.0;

  double max_ratio = 1.0;
  double min_ratio = 1.0;

  Storage1D<Math1D::Vector<ushort> > viterbi_alignment;
  if (viterbi_ilp_)
    viterbi_alignment.resize(source_sentence_.size());

  double dict_weight_sum = 0.0;
  for (uint i=0; i < nTargetWords_; i++) {
    dict_weight_sum += fabs(prior_weight_[i].sum());
  }

  if (parametric_distortion_)
    par2nonpar_distortion();

  ReducedIBM3DistortionModel fdistort_count(distortion_prob_.size(),MAKENAME(fdistort_count));
  for (uint J=0; J < fdistort_count.size(); J++) {
    fdistort_count[J].resize_dirty(distortion_prob_[J].xDim(), distortion_prob_[J].yDim());
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

  for (uint iter=1; iter <= nIter; iter++) {

    uint nViterbiBetter = 0;
    uint nViterbiWorse = 0;
  
    uint sum_iter = 0;

    fzero_count = 0.0;
    fnonzero_count = 0.0;

    for (uint J=0; J < fdistort_count.size(); J++) {
      fdistort_count[J].set_constant(0.0);
    }
    for (uint i=0; i < nTargetWords; i++) {
      fwcount[i].set_constant(0.0);
      ffert_count[i].set_constant(0.0);
    }

    max_perplexity = 0.0;

    for (uint s=0; s < source_sentence_.size(); s++) {

      if ((s% 250) == 0)
	std::cerr << "sentence pair #" << s << std::endl;
      
      const Storage1D<uint>& cur_source = source_sentence_[s];
      const Storage1D<uint>& cur_target = target_sentence_[s];
      const Math2D::Matrix<uint>& cur_lookup = slookup_[s];
      
      const uint curI = cur_target.size();
      const uint curJ = cur_source.size();
      const Math2D::Matrix<double>& cur_distort_count = fdistort_count[curJ-1];

      Math1D::NamedVector<uint> fertility(curI+1,0,MAKENAME(fertility));

      Math2D::NamedMatrix<long double> swap_move_prob(curJ,curJ,MAKENAME(swap_move_prob));
      Math2D::NamedMatrix<long double> expansion_move_prob(curJ,curI+1,MAKENAME(expansion_move_prob));
      
      long double best_prob = update_alignment_by_hillclimbing(s,sum_iter,fertility,
							       expansion_move_prob,swap_move_prob);

      // uint maxFert = 15;
      // if (2*curI <= curJ)
      // 	maxFert = 25;

      uint maxFert = curJ;

      long double viterbi_prob = 0.0;
      if (viterbi_ilp_) {
	viterbi_alignment[s] = best_known_alignment_[s];
	long double viterbi_prob = compute_viterbi_alignment_ilp(s,maxFert,viterbi_alignment[s]);
	//OPTIONAL
	//best_known_alignment_[s] = viterbi_alignment[s];
	//best_prob = update_alignment_by_hillclimbing(s,sum_iter,fertility,
	// 						   expansion_move_prob,swap_move_prob);
	//END_OPTIONAL
	viterbi_max_perplexity -= std::log(viterbi_prob);

	bool alignments_equal = true;
	for (uint j=0; j < curJ; j++) {
	  
	  if (best_known_alignment_[s][j] != viterbi_alignment[s][j])
	    alignments_equal = false;
	}

	if (!alignments_equal) {
	  
	  double ratio = viterbi_prob / best_prob;
	  
	  if (ratio > 1.01) {
	    nViterbiBetter++;
	    
	    // std::cerr << "pair #" << s << std::endl;
	    // std::cerr << "ilp prob:          " << viterbi_prob << std::endl;
	    // std::cerr << "ilp alignment: " << viterbi_alignment[s] << std::endl;
	    
	    // std::cerr << "hillclimbing prob: " << best_prob << std::endl;
	    // std::cerr << "hc. alignment: " << best_known_alignment_[s] << std::endl;
	  }
	  else if (ratio < 0.99) {
	    nViterbiWorse++;
	    
	    std::cerr << "pair #" << s << ": WORSE!!!!" << std::endl;
	    std::cerr << "ilp prob:          " << viterbi_prob << std::endl;
	    std::cerr << "ilp alignment: " << viterbi_alignment[s] << std::endl;
	    
	    std::cerr << "hillclimbing prob: " << best_prob << std::endl;
	    std::cerr << "hc. alignment: " << best_known_alignment_[s] << std::endl;
	  }
	  if (ratio > max_ratio) {
	    max_ratio = ratio;
	    
	    std::cerr << "pair #" << s << std::endl;
	    std::cerr << "ilp prob:          " << viterbi_prob << std::endl;
	    std::cerr << "ilp alignment: " << viterbi_alignment[s] << std::endl;
	    
	    std::cerr << "hillclimbing prob: " << best_prob << std::endl;
	    std::cerr << "hc. alignment: " << best_known_alignment_[s] << std::endl;
	  }
	  if (ratio < min_ratio) {
	    min_ratio = ratio;
	
	    std::cerr << "pair #" << s << std::endl;
	    std::cerr << "ilp prob:          " << viterbi_prob << std::endl;
	    std::cerr << "ilp alignment: " << viterbi_alignment[s] << std::endl;
	    
	    std::cerr << "hillclimbing prob: " << best_prob << std::endl;
	    std::cerr << "hc. alignment: " << best_known_alignment_[s] << std::endl;
	  }
	}
      }

      max_perplexity -= std::log(best_prob);
      
      const long double expansion_prob = expansion_move_prob.sum();
      const long double swap_prob =  0.5 * swap_move_prob.sum();

      const long double sentence_prob = best_prob + expansion_prob +  swap_prob;

      const long double inv_sentence_prob = 1.0 / sentence_prob;
      assert(!isnan(inv_sentence_prob));

      //std::cerr << "updating counts " << std::endl;

      double cur_zero_weight = best_prob;
      for (uint j=0; j < curJ; j++) {
	if (best_known_alignment_[s][j] == 0) {
	  
	  for (uint jj=j+1; jj < curJ; jj++) {
	    if (best_known_alignment_[s][jj] != 0)
	      cur_zero_weight += swap_move_prob(j,jj);
	  }
	}
      }
      cur_zero_weight *= inv_sentence_prob;
      cur_zero_weight /= (curJ - fertility[0]);
      
      fzero_count += cur_zero_weight * (fertility[0]);
      fnonzero_count += cur_zero_weight * (curJ - 2*fertility[0]);

      if (curJ >= 2*(fertility[0]+1)) {
	long double inc_zero_weight = 0.0;
	for (uint j=0; j < curJ; j++)
	  inc_zero_weight += expansion_move_prob(j,0);
	
	inc_zero_weight *= inv_sentence_prob;
	inc_zero_weight /= (curJ - fertility[0]-1);
	fzero_count += inc_zero_weight * (fertility[0]+1);
	fnonzero_count += inc_zero_weight * (curJ -2*(fertility[0]+1));
      }

      if (fertility[0] > 1) {
	long double dec_zero_weight = 0.0;
	for (uint j=0; j < curJ; j++) {
	  if (best_known_alignment_[s][j] == 0) {
	    for (uint i=1; i <= curI; i++)
	      dec_zero_weight += expansion_move_prob(j,i);
	  }
	}
      
	dec_zero_weight *= inv_sentence_prob;
	dec_zero_weight /= (curJ - fertility[0]+1);

	fzero_count += dec_zero_weight * (fertility[0]-1);
	fnonzero_count += dec_zero_weight * (curJ -2*(fertility[0]-1));
      }

      //increase counts for dictionary and distortion
      for (uint j=0; j < curJ; j++) {

	const uint s_idx = cur_source[j];
	const uint cur_aj = best_known_alignment_[s][j];

	long double addon = sentence_prob;
	for (uint i=0; i <= curI; i++) 
	  addon -= expansion_move_prob(j,i);
	for (uint jj=0; jj < curJ; jj++)
	  addon -= swap_move_prob(j,jj);

	addon *= inv_sentence_prob;

	assert(!isnan(addon));

	if (cur_aj != 0) {
	  fwcount[cur_target[cur_aj-1]][cur_lookup(j,cur_aj-1)] += addon;
	  cur_distort_count(j,cur_aj-1) += addon;
	  assert(!isnan(cur_distort_count(j,cur_aj-1)));
	}
	else {
	  fwcount[0][s_idx-1] += addon;
	}

	for (uint i=0; i <= curI; i++) {

	  if (i != cur_aj) {

	    long double addon = expansion_move_prob(j,i);
	    for (uint jj=0; jj < curJ; jj++) {
	      if (best_known_alignment_[s][jj] == i)
		addon += swap_move_prob(j,jj);
	    }
	    addon *= inv_sentence_prob;

	    assert(!isnan(addon));
	
	    if (i!=0) {
	      fwcount[cur_target[i-1]][cur_lookup(j,i-1)] += addon;
	      cur_distort_count(j,i-1) += addon;
	      assert(!isnan(cur_distort_count(j,i-1)));
	    }
	    else {
	      fwcount[0][s_idx-1] += addon;
	    }
	  }
	}
      }

      //update fertility counts
      for (uint i=1; i <= curI; i++) {

	const uint cur_fert = fertility[i];
	const uint t_idx = cur_target[i-1];

	long double addon = sentence_prob;
	for (uint j=0; j < curJ; j++) {
	  if (best_known_alignment_[s][j] == i) {
	    for (uint ii=0; ii <= curI; ii++)
	      addon -= expansion_move_prob(j,ii);
	  }
	  else
	    addon -= expansion_move_prob(j,i);
	}
	addon *= inv_sentence_prob;

	double daddon = (double) addon;
	if (!(daddon > 0.0)) {
	  std::cerr << "STRANGE: fractional weight " << daddon << " for sentence pair #" << s << " with "
		    << curJ << " source words and " << curI << " target words" << std::endl;
	  std::cerr << "best alignment prob: " << best_prob << std::endl;
	  std::cerr << "sentence prob: " << sentence_prob << std::endl;
	  std::cerr << "" << std::endl;
	}

	ffert_count[t_idx][cur_fert] += addon;

	//NOTE: swap moves do not change the fertilities
	if (cur_fert > 0) {
	  long double alt_addon = 0.0;
	  for (uint j=0; j < curJ; j++) {
	    if (best_known_alignment_[s][j] == i) {
	      for (uint ii=0; ii <= curI; ii++) {
		if (ii != i)
		  alt_addon += expansion_move_prob(j,ii);
	      }
	    }
	  }

	  ffert_count[t_idx][cur_fert-1] += inv_sentence_prob * alt_addon;
	}

	if (cur_fert+1 < fertility_prob_[t_idx].size()) {

	  long double alt_addon = 0.0;
	  for (uint j=0; j < curJ; j++) {
	    if (best_known_alignment_[s][j] != i) {
	      alt_addon += expansion_move_prob(j,i);
	    }
	  }

	  ffert_count[t_idx][cur_fert+1] += inv_sentence_prob * alt_addon;
	}
      }

//       std::cerr << "fzero_count: " << fzero_count << std::endl;
//       std::cerr << "fnonzero_count: " << fnonzero_count << std::endl;

      assert(!isnan(fzero_count));
      assert(!isnan(fnonzero_count));
    }

    //update p_zero_ and p_nonzero_
    double fsum = fzero_count + fnonzero_count;
    p_zero_ = fzero_count / fsum;
    p_nonzero_ = fnonzero_count / fsum;

    std::cerr << "new p_zero: " << p_zero_ << std::endl;

    //DEBUG
    uint nZeroAlignments = 0;
    uint nAlignments = 0;
    for (uint s=0; s < source_sentence_.size(); s++) {

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
    if (dict_weight_sum == 0.0) {
      for (uint i=0; i < nTargetWords; i++) {

	const double sum = fwcount[i].sum();
	
	if (sum > 1e-307) {
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
	  std::cerr << "WARNING: did not update dictionary entries because the sum was " << sum << std::endl;
	}
      }
    }
    else {

      for (uint i=0; i < nTargetWords; i++) {
	
	const double sum = fwcount[i].sum();
	const double prev_sum = dict_[i].sum();

	if (sum > 1e-307) {
	  const double inv_sum = 1.0 / sum;
	  assert(!isnan(inv_sum));
	  
	  for (uint k=0; k < fwcount[i].size(); k++) {
	    dict_[i][k] = fwcount[i][k] * prev_sum * inv_sum;
	  }
	}
      }

      double alpha = 100.0;
      if (iter > 2)
	alpha = 1.0;
      if (iter > 5)
	alpha = 0.1;

      dict_m_step(fwcount, prior_weight_, dict_, alpha, 45);
    }

    //update distortion prob from counts
    if (parametric_distortion_) {
      par_distortion_m_step(fdistort_count);
      par2nonpar_distortion();
    }
    else {

      for (uint J=0; J < distortion_prob_.size(); J++) {
	
//       std::cerr << "J:" << J << std::endl;
//       std::cerr << "distort_count: " << fdistort_count[J] << std::endl;
	for (uint i=0; i < distortion_prob_[J].yDim(); i++) {
	  
	  double sum = 0.0;
	  for (uint j=0; j < J+1; j++)
	    sum += fdistort_count[J](j,i);
	  
	  if (sum > 1e-307) {
	    const double inv_sum = 1.0 / sum;
	    assert(!isnan(inv_sum));
	    
	    for (uint j=0; j < J+1; j++) {
	      distortion_prob_[J](j,i) = inv_sum * fdistort_count[J](j,i);
	      if (isnan(distortion_prob_[J](j,i))) {
		std::cerr << "sum: " << sum << std::endl;
		std::cerr << "set to " << inv_sum << " * " << fdistort_count[J](j,i) << " = "
			  << (inv_sum * fdistort_count[J](j,i)) << std::endl;
	      }
	      assert(!isnan(fdistort_count[J](j,i)));
	      assert(!isnan(distortion_prob_[J](j,i)));
	    }
	  }
	  else {
	    std::cerr << "WARNING: did not update distortion count because sum was " << sum << std::endl;
	  }
	}
      }
    }

    for (uint i=1; i < nTargetWords; i++) {

      //std::cerr << "i: " << i << std::endl;

      const double sum = ffert_count[i].sum();

      if (sum > 1e-305) {

	if (fertility_prob_[i].size() > 0) {
	  assert(sum > 0.0);     
	  const double inv_sum = 1.0 / sum;
	  assert(!isnan(inv_sum));
	  
	  for (uint f=0; f < fertility_prob_[i].size(); f++)
	    fertility_prob_[i][f] = inv_sum * ffert_count[i][f];
	}
	else {
	  std::cerr << "WARNING: target word #" << i << " does not occur" << std::endl;
	}
      }
      else {
	std::cerr << "WARNING: did not update fertility count because sum was " << sum << std::endl;
      }
    }
    
    max_perplexity /= source_sentence_.size();
    viterbi_max_perplexity /= source_sentence_.size();

    std::cerr << "IBM3 max-perplexity in between iterations #" << (iter-1) << " and " << iter << ": "
	      << max_perplexity << std::endl;

    if (viterbi_ilp_) {
      std::cerr << "IBM3 viterbi max-perplexity in between iterations #" << (iter-1) << " and " << iter << ": "
		<< viterbi_max_perplexity << std::endl;

      std::cerr << "Viterbi-ILP better in " << nViterbiBetter << ", worse in " << nViterbiWorse << " cases." << std::endl;
      
      std::cerr << "max-ratio: " << max_ratio << std::endl;
      //std::cerr << "inv min-ratio: " << (1.0 / min_ratio) << std::endl;
    }

    if (possible_ref_alignments_.size() > 0) {
      
      std::cerr << "#### IBM3-AER in between iterations #" << (iter-1) << " and " << iter << ": " << AER() << std::endl;
      
      std::cerr << "#### IBM3-AER for Viterbi in between iterations #" << (iter-1) << " and " << iter << ": " 
		<< AER(viterbi_alignment) << std::endl;
      std::cerr << "#### IBM3-fmeasure in between iterations #" << (iter-1) << " and " << iter << ": " << f_measure() << std::endl;
      std::cerr << "#### IBM3-DAE/S in between iterations #" << (iter-1) << " and " << iter << ": " 
		<< DAE_S() << std::endl;
    }

    std::cerr << (((double) sum_iter) / source_sentence_.size()) << " average hillclimbing iterations per sentence pair" 
	      << std::endl; 
  }
}

void IBM3Trainer::train_viterbi(uint nIter, bool use_ilp) {

  std::cerr << "starting IBM3 training without constraints" << std::endl;

  double max_perplexity = 0.0;

  ReducedIBM3DistortionModel fdistort_count(distortion_prob_.size(),MAKENAME(fdistort_count));
  for (uint J=0; J < fdistort_count.size(); J++) {
    fdistort_count[J].resize_dirty(distortion_prob_[J].xDim(), distortion_prob_[J].yDim());
  }

  const uint nTargetWords = dict_.size();

  NamedStorage1D<Math1D::Vector<uint> > fwcount(nTargetWords,MAKENAME(fwcount));
  NamedStorage1D<Math1D::Vector<double> > ffert_count(nTargetWords,MAKENAME(ffert_count));

  for (uint i=0; i < nTargetWords; i++) {
    fwcount[i].resize(dict_[i].size());
    ffert_count[i].resize_dirty(fertility_prob_[i].size());
  }

  long double fzero_count;
  long double fnonzero_count;

  for (uint iter=1; iter <= nIter; iter++) {

    uint sum_iter = 0;

    fzero_count = 0.0;
    fnonzero_count = 0.0;

    for (uint J=0; J < fdistort_count.size(); J++) {
      fdistort_count[J].set_constant(0.0);
    }
    for (uint i=0; i < nTargetWords; i++) {
      fwcount[i].set_constant(0);
      ffert_count[i].set_constant(0.0);
    }

    max_perplexity = 0.0;

    for (uint s=0; s < source_sentence_.size(); s++) {

      if ((s% 1250) == 0)
	std::cerr << "sentence pair #" << s << std::endl;
      
      const Storage1D<uint>& cur_source = source_sentence_[s];
      const Storage1D<uint>& cur_target = target_sentence_[s];
      const Math2D::Matrix<uint>& cur_lookup = slookup_[s];
      
      const uint curI = cur_target.size();
      const uint curJ = cur_source.size();
      const Math2D::Matrix<double>& cur_distort_count = fdistort_count[curJ-1];

      Math1D::NamedVector<uint> fertility(curI+1,0,MAKENAME(fertility));

      Math2D::NamedMatrix<long double> swap_move_prob(curJ,curJ,MAKENAME(swap_move_prob));
      Math2D::NamedMatrix<long double> expansion_move_prob(curJ,curI+1,MAKENAME(expansion_move_prob));
      
      long double best_prob = update_alignment_by_hillclimbing(s,sum_iter,fertility,
							       expansion_move_prob,swap_move_prob);


#ifdef HAS_CBC
      if (use_ilp) {

	Math1D::Vector<ushort> alignment(curJ);
	compute_viterbi_alignment_ilp(s, curJ, alignment, 0.25);

	if (alignment_prob(s,alignment) > 1e-300) {

	  best_known_alignment_[s] = alignment;
	  
	  best_prob = update_alignment_by_hillclimbing(s,sum_iter,fertility,
						       expansion_move_prob,swap_move_prob);
	}
      }
#endif

      assert(2*fertility[0] <= curJ);

      // uint maxFert = 15;
      // if (2*curI <= curJ)
      // 	maxFert = 25;

      max_perplexity -= std::log(best_prob);
      
      //const long double sentence_prob = best_prob;

      //const long double inv_sentence_prob = 1.0 / sentence_prob;
      //assert(!isnan(inv_sentence_prob));

      //std::cerr << "updating counts " << std::endl;

      double cur_zero_weight = 1.0;
      cur_zero_weight /= (curJ - fertility[0]);
      
      fzero_count += cur_zero_weight * (fertility[0]);
      fnonzero_count += cur_zero_weight * (curJ - 2*fertility[0]);

      //increase counts for dictionary and distortion
      for (uint j=0; j < curJ; j++) {

	const uint s_idx = cur_source[j];
	const uint cur_aj = best_known_alignment_[s][j];

	if (cur_aj != 0) {
	  fwcount[cur_target[cur_aj-1]][cur_lookup(j,cur_aj-1)] += 1;
	  cur_distort_count(j,cur_aj-1) += 1.0;
	  assert(!isnan(cur_distort_count(j,cur_aj-1)));
	}
	else {
	  fwcount[0][s_idx-1] += 1;
	}
      }

      //update fertility counts
      for (uint i=1; i <= curI; i++) {

	const uint cur_fert = fertility[i];
	const uint t_idx = cur_target[i-1];

	ffert_count[t_idx][cur_fert] += 1.0;
      }

//       std::cerr << "fzero_count: " << fzero_count << std::endl;
//       std::cerr << "fnonzero_count: " << fnonzero_count << std::endl;

      assert(!isnan(fzero_count));
      assert(!isnan(fnonzero_count));
    }

    double fsum = fzero_count + fnonzero_count;
    p_zero_ = fzero_count / fsum;
    p_nonzero_ = fnonzero_count / fsum;

    /*** ICM stage ***/

    Math1D::NamedVector<uint> dict_sum(fwcount.size(),MAKENAME(dict_sum));
    for (uint k=0; k < fwcount.size(); k++)
      dict_sum[k] = fwcount[k].sum();

    uint nSwitches = 0;

    for (uint s=0; s < source_sentence_.size(); s++) {

      if ((s% 1250) == 0)
	std::cerr << "ICM, sentence pair #" << s << std::endl;
      
      const Storage1D<uint>& cur_source = source_sentence_[s];
      const Storage1D<uint>& cur_target = target_sentence_[s];
      const Math2D::Matrix<uint>& cur_lookup = slookup_[s];
      
      const uint curI = cur_target.size();
      const uint curJ = cur_source.size();

      Math1D::Vector<uint> cur_fertilities(curI+1,0);
      for (uint j=0; j < curJ; j++) {

	uint cur_aj = best_known_alignment_[s][j];
	cur_fertilities[cur_aj]++;
      }

      Math2D::Matrix<double>& cur_distort_count = fdistort_count[curJ-1];
      
      for (uint j=0; j < curJ; j++) {

	for (uint i = 0; i <= curI; i++) {

	  uint cur_aj = best_known_alignment_[s][j];
	  uint cur_word = (cur_aj == 0) ? 0 : cur_target[cur_aj-1];

	  /**** dict ***/
	  //std::cerr << "i: " << i << ", cur_aj: " << cur_aj << std::endl;

	  bool allowed = (cur_aj != i && (i != 0 || 2*cur_fertilities[0]+2 <= curJ));

	  if (cur_aj > 0 && i > 0 && cur_target[cur_aj-1] == cur_target[i-1])
	    allowed = false;

	  if (allowed) {
	    //if (cur_aj != i && (i != 0 || 2*cur_fertilities[0]+2 <= curJ)) {

	    uint new_target_word = (i == 0) ? 0 : cur_target[i-1];

	    //std::cerr << "cur_word: " << cur_word << std::endl;
	    //std::cerr << "new_word: " << new_target_word << std::endl;

	    double change = 0.0;

	    Math1D::Vector<uint>& cur_dictcount = fwcount[cur_word]; 
	    Math1D::Vector<uint>& hyp_dictcount = fwcount[new_target_word]; 


	    if (cur_word != new_target_word) {

	      uint cur_idx = (cur_aj == 0) ? cur_source[j]-1 : cur_lookup(j,cur_aj-1);

	      double cur_dictsum = dict_sum[cur_word];

	      uint hyp_idx = (i == 0) ? cur_source[j]-1 : cur_lookup(j,i-1);

	      if (dict_sum[new_target_word] > 0)
		change -= double(dict_sum[new_target_word]) * std::log( dict_sum[new_target_word] );
	      change += double(dict_sum[new_target_word]+1.0) * std::log( dict_sum[new_target_word]+1.0 );

	      if (fwcount[new_target_word][hyp_idx] > 0)
	       	change -= double(fwcount[new_target_word][hyp_idx]) * 
	       	  (-std::log(fwcount[new_target_word][hyp_idx]));
	      else
	       	change += prior_weight_[new_target_word][hyp_idx]; 

	      change += double(fwcount[new_target_word][hyp_idx]+1) * 
	       	(-std::log(fwcount[new_target_word][hyp_idx]+1.0));

	      change -= double(cur_dictsum) * std::log(cur_dictsum);
	      if (cur_dictsum > 1)
		change += double(cur_dictsum-1) * std::log(cur_dictsum-1.0);
	      
	      change -= - double(cur_dictcount[cur_idx]) * std::log(cur_dictcount[cur_idx]);
	      
	      if (cur_dictcount[cur_idx] > 1) {
		change += double(cur_dictcount[cur_idx]-1) * (-std::log(cur_dictcount[cur_idx]-1));
	      }
	      else
		change -= prior_weight_[cur_word][cur_idx];


	      /***** fertilities (only affected if the old and new target word differ) ****/
	      //std::cerr << "fert-part" << std::endl;
	      
	      //note: currently not updating f_zero / f_nonzero
	      if (cur_aj == 0) {
		
		uint zero_fert = cur_fertilities[0];
		
		change -= - std::log(ldchoose(curJ-zero_fert,zero_fert));
		for (uint k=1; k <= zero_fert; k++)
		  change -= -std::log(p_zero_);
		for (uint k=1; k <= curJ-2*zero_fert; k++)
		  change -= -std::log(p_nonzero_);
		
		if (och_ney_empty_word_) {
		  
		  for (uint k=1; k<= zero_fert; k++)
		    change -= -std::log(((long double) k) / curJ);
		}
		
		uint new_zero_fert = zero_fert-1;
		change += - std::log(ldchoose(curJ-new_zero_fert,new_zero_fert));
		for (uint k=1; k <= new_zero_fert; k++)
		  change += -std::log(p_zero_);
		for (uint k=1; k <= curJ-2*new_zero_fert; k++)
		  change += -std::log(p_nonzero_);
		
		if (och_ney_empty_word_) {
		  
		  for (uint k=1; k<= new_zero_fert; k++)
		    change += -std::log(((long double) k) / curJ);
		}
	      }
	      else {
		double c = ffert_count[cur_word][cur_fertilities[cur_aj]];
		change -= -c * std::log(c);
		if (c > 1)
		  change += -(c-1) * std::log(c-1);
		
		double c2 = ffert_count[cur_word][cur_fertilities[cur_aj]-1];
		
		if (c2 > 0)
		  change -= -c2 * std::log(c2);
		change += -(c2+1) * std::log(c2+1);
	      }
	      
	      //std::cerr << "----" << std::endl;

	      if (i == 0) {

		uint zero_fert = cur_fertilities[0];

		change -= - std::log(ldchoose(curJ-zero_fert,zero_fert));
		for (uint k=1; k <= zero_fert; k++)
		  change -= -std::log(p_zero_);
		for (uint k=1; k <= curJ-2*zero_fert; k++)
		  change -= -std::log(p_nonzero_);
		
		if (och_ney_empty_word_) {
		  
		  for (uint k=1; k<= zero_fert; k++)
		    change -= -std::log(((long double) k) / curJ);
		}
		
		uint new_zero_fert = zero_fert+1;
		change += - std::log(ldchoose(curJ-new_zero_fert,new_zero_fert));
		for (uint k=1; k <= new_zero_fert; k++)
		  change += -std::log(p_zero_);
		for (uint k=1; k <= curJ-2*new_zero_fert; k++)
		  change += -std::log(p_nonzero_);
		
		if (och_ney_empty_word_) {
		
		  for (uint k=1; k<= new_zero_fert; k++)
		    change += -std::log(((long double) k) / curJ);
		}
	      }
	      else {
		
		double c = ffert_count[new_target_word][cur_fertilities[i]];
		change -= -c * std::log(c);
		if (c > 1)
		  change += -(c-1) * std::log(c-1);
		else
		  change -= l0_fertpen_;
		
		double c2 = ffert_count[new_target_word][cur_fertilities[i]+1];
		if (c2 > 0)
		  change -= -c2 * std::log(c2);
		else
		  change += l0_fertpen_;
		change += -(c2+1) * std::log(c2+1);
	      }
	    }

	    //std::cerr << "dist" << std::endl;

	    /***** distortion ****/
	    if (cur_aj != 0) {

	      double c = cur_distort_count(j,cur_aj-1);
	      
	      change -= -c * std::log(c);
	      if (c > 1)
		change += -(c-1) * std::log(c-1);
	    }
	    if (i != 0) {

	      double c = cur_distort_count(j,i-1);
	      if (c > 0)
		change -= -c * std::log(c);
	      change += -(c+1) * std::log(c+1);
	    }


	    if (change < -0.01) {

	      //std::cerr << "changing!!" << std::endl;
	   
	      best_known_alignment_[s][j] = i;
	      nSwitches++;

	      //std::cerr << "A" << std::endl;

	      uint cur_idx = (cur_aj == 0) ? cur_source[j]-1 : cur_lookup(j,cur_aj-1);

	      uint hyp_idx = (i == 0) ? cur_source[j]-1 : cur_lookup(j,i-1);

	      //dict
	      cur_dictcount[cur_idx] -= 1;
	      hyp_dictcount[hyp_idx] += 1;
	      dict_sum[cur_word] -= 1;
	      dict_sum[new_target_word] += 1;

	      //std::cerr << "B" << std::endl;

	      //fert
	      if (cur_word != 0) {
		uint prev_fert = cur_fertilities[cur_aj];
		assert(prev_fert != 0);
		ffert_count[cur_word][prev_fert] -= 1;
		ffert_count[cur_word][prev_fert-1] += 1;
	      }
	      if (new_target_word != 0) {
		uint prev_fert = cur_fertilities[i];
		ffert_count[new_target_word][prev_fert] -= 1;
		ffert_count[new_target_word][prev_fert+1] += 1;
	      }

	      //std::cerr << "C" << std::endl;

	      cur_fertilities[cur_aj]--;
	      cur_fertilities[i]++;

	      //distort
	      if (cur_aj != 0)
		cur_distort_count(j,cur_aj-1)--;
	      if (i != 0)
		cur_distort_count(j,i-1)++;

	      //std::cerr << "D" << std::endl;
	    }
	  }
	}
      }
    }

    std::cerr << nSwitches << " changes in ICM stage" << std::endl;

    //update p_zero_ and p_nonzero_
    fsum = fzero_count + fnonzero_count;
    p_zero_ = fzero_count / fsum;
    p_nonzero_ = fnonzero_count / fsum;

    std::cerr << "new p_zero: " << p_zero_ << std::endl;

    //DEBUG
    uint nZeroAlignments = 0;
    uint nAlignments = 0;
    for (uint s=0; s < source_sentence_.size(); s++) {

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

    //update distortion prob from counts
    for (uint J=0; J < distortion_prob_.size(); J++) {

//       std::cerr << "J:" << J << std::endl;
//       std::cerr << "distort_count: " << fdistort_count[J] << std::endl;
      for (uint i=0; i < distortion_prob_[J].yDim(); i++) {

	double sum = 0.0;
	for (uint j=0; j < J+1; j++)
	  sum += fdistort_count[J](j,i);

	if (sum > 1e-305) {
	  const double inv_sum = 1.0 / sum;
	  assert(!isnan(inv_sum));
	  
	  for (uint j=0; j < J+1; j++) {
	    distortion_prob_[J](j,i) = inv_sum * fdistort_count[J](j,i);
	    if (isnan(distortion_prob_[J](j,i))) {
	      std::cerr << "sum: " << sum << std::endl;
	      std::cerr << "set to " << inv_sum << " * " << fdistort_count[J](j,i) << " = "
			<< (inv_sum * fdistort_count[J](j,i)) << std::endl;
	    }
	    assert(!isnan(fdistort_count[J](j,i)));
	    assert(!isnan(distortion_prob_[J](j,i)));
	  }
	}
	else {
	  //std::cerr << "WARNING: did not update distortion count because sum was " << sum << std::endl;
	}
      }
    }

    for (uint i=1; i < nTargetWords; i++) {

      //std::cerr << "i: " << i << std::endl;

      const double sum = ffert_count[i].sum();

      if (sum > 1e-305) {

	if (fertility_prob_[i].size() > 0) {
	  assert(sum > 0.0);     
	  const double inv_sum = 1.0 / sum;
	  assert(!isnan(inv_sum));
	  
	  for (uint f=0; f < fertility_prob_[i].size(); f++)
	    fertility_prob_[i][f] = inv_sum * ffert_count[i][f];
	}
	else {
	  std::cerr << "WARNING: target word #" << i << " does not occur" << std::endl;
	}
      }
      else {
	std::cerr << "WARNING: did not update fertility count because sum was " << sum << std::endl;
      }
    }
    
    max_perplexity = 0.0;
    for (uint s=0; s < source_sentence_.size(); s++)
      max_perplexity -= std::log(alignment_prob(s,best_known_alignment_[s]));

    for (uint i=0; i < fwcount.size(); i++)
      for (uint k=0; k < fwcount[i].size(); k++)
	if (fwcount[i][k] > 0)
	  max_perplexity += prior_weight_[i][k];

    max_perplexity /= source_sentence_.size();

    std::cerr << "IBM3 energy after iteration #" << iter << ": "
	      << max_perplexity << std::endl;


    if (possible_ref_alignments_.size() > 0) {
      
      std::cerr << "#### IBM3-AER in between iterations #" << (iter-1) << " and " << iter << ": " << AER() << std::endl;
      std::cerr << "#### IBM3-fmeasure in between iterations #" << (iter-1) << " and " << iter << ": " << f_measure() << std::endl;
      std::cerr << "#### IBM3-DAE/S in between iterations #" << (iter-1) << " and " << iter << ": " 
		<< DAE_S() << std::endl;
    }

    std::cerr << (((double) sum_iter) / source_sentence_.size()) << " average hillclimbing iterations per sentence pair" 
	      << std::endl; 
  }
}


long double IBM3Trainer::compute_itg_viterbi_alignment_noemptyword(uint s, bool extended_reordering) {

  std::cerr << "******** compute_itg_viterbi_alignment_noemptyword(" << s << ") **********" << std::endl;

  const Storage1D<uint>& cur_source = source_sentence_[s];
  const Storage1D<uint>& cur_target = target_sentence_[s];
  const Math2D::Matrix<uint>& cur_lookup = slookup_[s];
  
  const uint curI = cur_target.size();
  const uint curJ = cur_source.size();
  const Math2D::Matrix<double>& cur_distort_prob = distortion_prob_[curJ-1];
  
  Math2D::NamedMatrix<long double> score0(curI,curI,0.0,MAKENAME(score0));

  for (uint i=0; i < curI; i++) {
    long double prob = fertility_prob_[cur_target[i]][0];
    score0(i,i) = prob;
    for (uint ii=i+1; ii < curI; ii++) {
      prob *= fertility_prob_[cur_target[ii]][0];
      score0(i,ii) = prob;
    }
  }

  NamedStorage1D<Math3D::NamedTensor<long double> > score(curJ+1,MAKENAME(score));
  NamedStorage1D<Math3D::NamedTensor<uint> > trace(curJ+1,MAKENAME(trace));

  score[1].set_name("score[1]");
  score[1].resize(curJ,curI,curI,0.0);
  trace[1].set_name("trace[1]");
  trace[1].resize(curJ,curI,curI,MAX_UINT);

  Math3D::NamedTensor<long double>& score1 = score[1];
  Math3D::NamedTensor<uint>& trace1 = trace[1];

  for (uint j=0; j < curJ; j++) {
    
    for (uint i=0; i < curI; i++) {
      for (uint ii=i; ii < curI; ii++) {

	const long double zero_prob = score0(i,ii);

	long double best_prob = 0.0;
	
	for (uint iii=i; iii <= ii; iii++) {

	  const uint t_idx = cur_target[iii];

	  long double hyp_prob = zero_prob * fertility_prob_[t_idx][1]
	    * cur_distort_prob(j,iii) * dict_[t_idx][cur_lookup(j,iii)] / fertility_prob_[t_idx][0];

	  if (hyp_prob > best_prob) {
	    best_prob = hyp_prob;
	    trace1(j,i,ii) = iii;
	  }
	}

	score1(j,i,ii) = best_prob;
      }
    }
  }

  for (uint J=2; J <= curJ; J++) {

    std::cerr << "J: " << J << std::endl;

    score[J].set_name("score[" + toString(J) + "]");
    score[J].resize(curJ,curI,curI,0.0);
    trace[J].set_name("trace[" + toString(J) + "]");
    trace[J].resize(curJ,curI,curI,MAX_UINT);

    long double Jfac = ldfac(J);

    Math3D::NamedTensor<uint>& traceJ = trace[J];
    Math3D::NamedTensor<long double>& scoreJ = score[J];
    
    for (uint I=1; I <= curI; I++) {

      for (uint i=0; i < (curI-(I-1)); i++) {

	const uint ii=i+I-1;
	assert(ii < curI);
	assert(ii >= i);

	const uint ti = cur_target[i];
	const uint tii = cur_target[ii];

	for (uint j=0; j < (curJ-(J-1)); j++) {
	  
	  const uint jj = j + J -1; 
	  assert(jj < curJ);
	  assert(jj >= j);
	  
	  long double best_prob = 0.0;
	  uint trace_entry = MAX_UINT;

	  if (I==1) {
	    best_prob = 1.0;
	    for (uint jjj = j; jjj <= jj; jjj++)
	      best_prob *= dict_[ti][cur_lookup(jjj,i)] * cur_distort_prob(jjj,i);
	    best_prob *= fertility_prob_[ti][J] * Jfac;
	    trace_entry = MAX_UINT;
	  }
	  else {

	    if (extended_reordering && I <= 10 && J <= 10) {

	      long double base_prob;
	      base_prob = ldfac(J-1) * fertility_prob_[ti][J-1] * fertility_prob_[tii][1];
	      for (uint iii=i+1; iii <= ii-1; iii++)
		base_prob *= fertility_prob_[cur_target[iii]][0];
	      
	      for (uint k=1; k < J-1; k++) {
		
		long double hyp_prob = base_prob;
		for (uint l=0; l < J; l++) {
		  
		  if (l==k)
		    hyp_prob *= dict_[tii][cur_lookup(j+l,ii)] * cur_distort_prob(j+l,ii);
		  else
		    hyp_prob *= dict_[ti][cur_lookup(j+l,i)] * cur_distort_prob(j+l,i);
		  
		}
		if (hyp_prob > best_prob) {
		  best_prob = hyp_prob;
		    
		  trace_entry = 0xC0000000;
		  uint base = 1;
		  for (uint l=0; l < J; l++) {
		    if (l==k)
		      trace_entry += base;
		    base *= 2;
		  }
		}
	      }
		
	      base_prob = ldfac(J-1) * fertility_prob_[tii][J-1] * fertility_prob_[ti][1];
	      for (uint iii=i+1; iii <= ii-1; iii++)
		base_prob *= fertility_prob_[cur_target[iii]][0];
		
	      for (uint k=1; k < J-1; k++) {
		  
		long double hyp_prob = base_prob;
		for (uint l=0; l < J; l++) {
		    
		  if (l==k)
		    hyp_prob *= dict_[ti][cur_lookup(j+l,i)] * cur_distort_prob(j+l,i);
		  else
		    hyp_prob *= dict_[tii][cur_lookup(j+l,ii)] * cur_distort_prob(j+l,ii);
		    
		}
		if (hyp_prob > best_prob) {
		  best_prob = hyp_prob;
		  
		  trace_entry = 0xC0000000;
		  uint base = 1;
		  for (uint l=0; l < J; l++) {
		    if (l!=k)
		      trace_entry += base;
		    base *= 2;
		  }
		}
	      }
	    }

	    //1.) consider extending the target interval by a zero-fertility word
	    const double left_extend_prob  = scoreJ(j,i+1,ii) * fertility_prob_[ti][0];
	    if (left_extend_prob > best_prob) {
	      best_prob = left_extend_prob;
	      trace_entry = MAX_UINT - 1;
	    }
	    const double right_extend_prob = scoreJ(j,i,ii-1) * fertility_prob_[tii][0];
	    if (right_extend_prob > best_prob) {
	      best_prob = right_extend_prob;
	      trace_entry = MAX_UINT - 2;
	    }

	    //2.) consider splitting both source and target interval
	    
	    for (uint split_j = j; split_j < jj; split_j++) {
	      
	      //partitioning into [j,split_j] and [split_j+1,jj]
	      
	      const uint J1 = split_j - j + 1;
	      const uint J2 = jj - split_j;
	      assert(J1 >= 1 && J1 < J);
	      assert(J2 >= 1 && J2 < J);
	      assert(J1 + J2 == J);
	      const uint split_j_p1 = split_j + 1;
	      
	      const Math3D::Tensor<long double>& score_J1 = score[J1];
	      const Math3D::Tensor<long double>& score_J2 = score[J2];
	      
	      for (uint split_i = i; split_i < ii; split_i++) {
		
		//partitioning into [i,split_i] and [split_i+1,ii]
		
// 		const uint I1 = split_i - i + 1;
// 		const uint I2 = ii - split_i;
// 		assert(I1 >= 1 && I1 < I);
// 		assert(I2 >= 1 && I2 < I);

		const long double hyp_monotone_prob = score_J1(j,i,split_i) * score_J2(split_j_p1,split_i+1,ii);

		if (hyp_monotone_prob > best_prob) {
		  best_prob = hyp_monotone_prob;
		  trace_entry = 2*(split_j * curI + split_i);
		}
		
		const long double hyp_invert_prob = score_J2(split_j_p1,i,split_i) * score_J1(j,split_i+1,ii);

		if (hyp_invert_prob > best_prob) {
		  best_prob = hyp_invert_prob;
		  trace_entry = 2*(split_j * curI + split_i) + 1;
		}
	      }
	    }
	  }

	  scoreJ(j,i,ii) = best_prob;
	  traceJ(j,i,ii) = trace_entry;
	}
      }
    }
  }

  best_known_alignment_[s].set_constant(0);
  itg_traceback(s,trace,curJ,0,0,curI-1);

  return score[curJ](0,0,curI-1);
}

void IBM3Trainer::itg_traceback(uint s, const NamedStorage1D<Math3D::NamedTensor<uint> >& trace, 
				uint J, uint j, uint i, uint ii) {


  //std::cerr << "****itg_traceback(" << J << "," << j << "," << i << "," << ii << ")" << std::endl; 

  uint trace_entry = trace[J](j,i,ii);

  if (J == 1) {
    best_known_alignment_[s][j] = trace_entry+1;
  }
  else if (i==ii) {
    assert(trace_entry == MAX_UINT);
    
    for (uint jj=j; jj < j+J; jj++)
      best_known_alignment_[s][jj] = i+1;
  }
  else if (trace_entry == MAX_UINT-1) {
    itg_traceback(s,trace,J,j,i+1,ii);
  }
  else if (trace_entry == MAX_UINT-2) {
    itg_traceback(s,trace,J,j,i,ii-1);
  }
  else if (trace_entry >= 0xC0000000) {

    uint temp = trace_entry & 0x3FFFFFF;
    for (uint k=0; k < J; k++) {

      uint bit = (temp % 2);
      best_known_alignment_[s][j+k] = (bit == 1) ? (ii+1) : (i+1);
      temp /= 2;
    }
  }
  else {
    
    bool reverse = ((trace_entry % 2) == 1);
    trace_entry /= 2;

    uint split_i = trace_entry % target_sentence_[s].size();
    uint split_j = trace_entry / target_sentence_[s].size();

//     std::cerr << "split_i: " << split_i << ", split_j: " << split_j << ", reverse: " << reverse 
// 	      << std::endl;

    assert(split_i < target_sentence_[s].size());
    assert(split_j < source_sentence_[s].size());
    
    const uint J1 = split_j - j + 1;
    const uint J2 = J - J1;

    if (!reverse) {
      itg_traceback(s,trace,J1,j,i,split_i);
      itg_traceback(s,trace,J2,split_j+1,split_i+1,ii);
    }
    else {
      itg_traceback(s,trace,J2,split_j+1,i,split_i);
      itg_traceback(s,trace,J1,j,split_i+1,ii);
    }
  }

}

void IBM3Trainer::train_with_itg_constraints(uint nIter, bool extended_reordering, bool verbose) {


  ReducedIBM3DistortionModel fdistort_count(distortion_prob_.size(),MAKENAME(fdistort_count));
  for (uint J=0; J < fdistort_count.size(); J++) {
    fdistort_count[J].resize_dirty(distortion_prob_[J].xDim(), distortion_prob_[J].yDim());
  }

  const uint nTargetWords = dict_.size();

  NamedStorage1D<Math1D::Vector<uint> > fwcount(nTargetWords,MAKENAME(fwcount));
  NamedStorage1D<Math1D::Vector<double> > ffert_count(nTargetWords,MAKENAME(ffert_count));

  for (uint i=0; i < nTargetWords; i++) {
    fwcount[i].resize(dict_[i].size());
    ffert_count[i].resize_dirty(fertility_prob_[i].size());
  }

  long double fzero_count;
  long double fnonzero_count;


  for (uint iter=1; iter <= nIter; iter++) {


    fzero_count = 0.0;
    fnonzero_count = 0.0;

    for (uint i=0; i < nTargetWords; i++) {
      fwcount[i].set_constant(0.0);
      ffert_count[i].set_constant(0.0);
    }

    for (uint J=0; J < distortion_prob_.size(); J++) {
      fdistort_count[J].set_constant(0.0);
    }

    double max_perplexity = 0.0;

    uint nBetter = 0;
    uint nEqual = 0;

    for (uint s=0; s < source_sentence_.size(); s++) {

      long double hillclimbprob = alignment_prob(s,best_known_alignment_[s]);

      long double prob = compute_itg_viterbi_alignment_noemptyword(s,extended_reordering);

      long double actual_prob = prob * pow(p_nonzero_,source_sentence_[s].size());
      
      if (verbose) {
	long double check_prob = alignment_prob(s,best_known_alignment_[s]);
	long double check_ratio = actual_prob / check_prob;
	
	if (check_prob == hillclimbprob)
	  nEqual++;
	else if (check_prob > hillclimbprob)
	  nBetter++;
	
	//std::cerr << "found alignment: " << best_known_alignment_[s] << std::endl;
	//std::cerr << "check_ratio: " << check_ratio << std::endl;
	assert(check_ratio >= 0.999 && check_ratio < 1.001);
      }

      max_perplexity -= std::log(actual_prob);

      const Storage1D<uint>&  cur_source = source_sentence_[s];
      const Storage1D<uint>&  cur_target = target_sentence_[s];
      const Math2D::Matrix<uint>& cur_lookup = slookup_[s];      

      const uint curJ = source_sentence_[s].size();
      const uint curI = target_sentence_[s].size();
 
      Math2D::Matrix<double>& cur_distort_count = fdistort_count[curJ-1];

      Math1D::Vector<uint> fertility(curI+1,0);
      for (uint j=0; j < curJ; j++) {
	fertility[best_known_alignment_[s][j]]++;
      }

      //currently implementing Viterbi training

      double cur_zero_weight = 1.0;
      cur_zero_weight /= (curJ - fertility[0]);
      
      fzero_count += cur_zero_weight * (fertility[0]);
      fnonzero_count += cur_zero_weight * (curJ - 2*fertility[0]);

      //increase counts for dictionary and distortion
      for (uint j=0; j < curJ; j++) {

	const uint s_idx = cur_source[j];
	const uint cur_aj = best_known_alignment_[s][j];

	if (cur_aj != 0) {
	  fwcount[cur_target[cur_aj-1]][cur_lookup(j,cur_aj-1)] += 1;
	  cur_distort_count(j,cur_aj-1) += 1.0;
	  assert(!isnan(cur_distort_count(j,cur_aj-1)));
	}
	else {
	  fwcount[0][s_idx-1] += 1;
	}
      }

      //update fertility counts
      for (uint i=1; i <= curI; i++) {

	const uint cur_fert = fertility[i];
	const uint t_idx = cur_target[i-1];

	ffert_count[t_idx][cur_fert] += 1.0;
      }      
    }
    
    max_perplexity /= source_sentence_.size();

    //update p_zero_ and p_nonzero_
    double fsum = fzero_count + fnonzero_count;
    p_zero_ = fzero_count / fsum;
    p_nonzero_ = fnonzero_count / fsum;

    std::cerr << "new p_zero: " << p_zero_ << std::endl;

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

    //update distortion prob from counts
    for (uint J=0; J < distortion_prob_.size(); J++) {

//       std::cerr << "J:" << J << std::endl;
//       std::cerr << "distort_count: " << fdistort_count[J] << std::endl;
      for (uint i=0; i < distortion_prob_[J].yDim(); i++) {

	double sum = 0.0;
	for (uint j=0; j < J+1; j++)
	  sum += fdistort_count[J](j,i);

	if (sum > 1e-305) {
	  const double inv_sum = 1.0 / sum;
	  assert(!isnan(inv_sum));
	  
	  for (uint j=0; j < J+1; j++) {
	    distortion_prob_[J](j,i) = inv_sum * fdistort_count[J](j,i);
	    if (isnan(distortion_prob_[J](j,i))) {
	      std::cerr << "sum: " << sum << std::endl;
	      std::cerr << "set to " << inv_sum << " * " << fdistort_count[J](j,i) << " = "
			<< (inv_sum * fdistort_count[J](j,i)) << std::endl;
	    }
	    assert(!isnan(fdistort_count[J](j,i)));
	    assert(!isnan(distortion_prob_[J](j,i)));
	  }
	}
	else {
	  //std::cerr << "WARNING: did not update distortion count because sum was " << sum << std::endl;
	}
      }
    }

    for (uint i=1; i < nTargetWords; i++) {

      //std::cerr << "i: " << i << std::endl;

      const double sum = ffert_count[i].sum();

      if (sum > 1e-305) {

	if (fertility_prob_[i].size() > 0) {
	  assert(sum > 0.0);     
	  const double inv_sum = 1.0 / sum;
	  assert(!isnan(inv_sum));
	  
	  for (uint f=0; f < fertility_prob_[i].size(); f++)
	    fertility_prob_[i][f] = inv_sum * ffert_count[i][f];
	}
	else {
	  std::cerr << "WARNING: target word #" << i << " does not occur" << std::endl;
	}
      }
      else {
	std::cerr << "WARNING: did not update fertility count because sum was " << sum << std::endl;
      }
    }
    

    if (possible_ref_alignments_.size() > 0) {
      
      std::cerr << "#### IBM3-AER in between iterations #" << (iter-1) << " and " << iter << ": " << AER() << std::endl;
      std::cerr << "#### IBM3-fmeasure in between iterations #" << (iter-1) << " and " << iter << ": " << f_measure() << std::endl;
      std::cerr << "#### IBM3-DAE/S in between iterations #" << (iter-1) << " and " << iter << ": " 
		<< DAE_S() << std::endl;
    }


    std::cerr << "max-perplexility after iteration #" << (iter - 1) << ": " << max_perplexity << std::endl;
    if (verbose) {
      std::cerr << "itg-coinstraints are eqaul to hillclimbing in " << nEqual << " cases" << std::endl;
      std::cerr << "itg-coinstraints are better than hillclimbing in " << nBetter << " cases" << std::endl;
    }

  }
}

long double IBM3Trainer::compute_ibmconstrained_viterbi_alignment_noemptyword(uint s, uint maxFertility, 
									      uint nMaxSkips) {

  std::cerr << "******** compute_ibmconstrained_viterbi_alignment_noemptyword2(" << s << ") **********" << std::endl;

  assert(maxFertility >= 1);

  //convention here: target positions start at 0, source positions start at 1 
  // (so we can express that no source position was covered yet)

  const Storage1D<uint>& cur_source = source_sentence_[s];
  const Storage1D<uint>& cur_target = target_sentence_[s];
  const Math2D::Matrix<uint>& cur_lookup = slookup_[s];
  
  const uint curI = cur_target.size();
  const uint curJ = cur_source.size();
  const Math2D::Matrix<double>& cur_distort_prob = distortion_prob_[curJ-1];

  const uint nStates = first_state_[curJ+1];

  std::cerr << "curJ: " << curJ << ", curI: " << curI << std::endl;
  std::cerr << nStates << " active states" << std::endl;

  maxFertility = std::min(maxFertility,curJ);

  NamedStorage1D<Math2D::NamedMatrix<long double> > score(curI,MAKENAME(score));

  NamedStorage1D<Math2D::NamedMatrix<uchar> > state_trace(curI,MAKENAME(state_trace));
  NamedStorage1D<Math1D::NamedVector<uchar> > fert_trace(curI,MAKENAME(fert_trace));

  Math1D::NamedVector<long double> best_prev_score(nStates,0.0,MAKENAME(best_prev_score));

  score[0].set_name(MAKENAME(score[0]));
  score[0].resize(nStates,maxFertility+1,0.0);

  const uint start_allfert_max_reachable_j  = std::min(curJ, nMaxSkips + maxFertility);
  const uint start_allfert_max_reachable_state = first_state_[start_allfert_max_reachable_j+1]-1;

  fert_trace[0].set_name("fert_trace[0]");
  fert_trace[0].resize(start_allfert_max_reachable_state+1/*,255*/);
  state_trace[0].set_name("state_trace[0]");
  state_trace[0].resize(start_allfert_max_reachable_state+1,maxFertility/*,255*/);

  const uint t_start = cur_target[0];

  //initialize fertility 0
  score[0](0,0) = 1.0;

  //initialize for fertility 1
  for (uint state = 0; state <= start_allfert_max_reachable_state; state++) {

    const uint set_idx = coverage_state_(0,state);
    const uint maxUncoveredPos = (set_idx == 0) ? 0 : uncovered_set_(nMaxSkips-1,set_idx);
    const uint max_covered_j = coverage_state_(1,state);

    assert(max_covered_j <= curJ);
    if (max_covered_j <= nMaxSkips+1 && max_covered_j == maxUncoveredPos+1) {
	
      //check if all positions until max_covered_j are skipped
      const uint nCurUncoveredPositions = nUncoveredPositions_[set_idx];
      
      //TODO: pre-generate a list of start states
      bool consecutive = true;
      for (uint k=1; k <= nCurUncoveredPositions; k++) {
	
	if (uncovered_set_(nMaxSkips-nCurUncoveredPositions+(k-1),set_idx) != k) {
	  consecutive = false;
	  break;
	}
      }
      
      if (consecutive)
	score[0](state,1) = dict_[t_start][cur_lookup(max_covered_j-1,0)] 
	  * cur_distort_prob(max_covered_j-1,0);
    }
  }
 
  //initialize for fertility 2
  for (uint fert=2; fert <= maxFertility; fert++) {

    const uint curfert_max_reachable_j  = std::min(curJ, nMaxSkips + fert);
    const uint curfert_max_reachable_state = first_state_[curfert_max_reachable_j+1]-1;

    for (uint state = 0; state <= curfert_max_reachable_state; state++) {

      assert(coverage_state_(1,state) <= curJ);
	
      long double best_score = 0.0;
      uchar trace_entry = 255;
      
      const uint nPredecessors = predecessor_coverage_states_[state].yDim();
      assert(nPredecessors < 255);
      
      for (uint p=0; p < nPredecessors; p++) {
	
	const uint prev_state = predecessor_coverage_states_[state](0,p);
	const uint cover_j = predecessor_coverage_states_[state](1,p);
	
	assert(cover_j <= curJ);
	
	const long double hyp_score = score[0](prev_state,fert-1) 
	  * dict_[t_start][cur_lookup(cover_j-1,0)] * cur_distort_prob(cover_j-1,0);
	
	if (hyp_score > best_score) {
	  best_score = hyp_score;
	  trace_entry = p;
	}
      }
      
      score[0](state,fert) = best_score;
      state_trace[0](state,fert-1) = trace_entry;
    }
  }

  //finally include fertility probabilities
  for (uint fert = 0; fert <= maxFertility; fert++) {
    long double fert_factor = (fertility_prob_[t_start].size() > fert) ? fertility_prob_[t_start][fert] : 0.0;
    if (fert > 1)
      fert_factor *= ldfac(fert);

    for (uint state = 0; state <= start_allfert_max_reachable_state; state++) 
      score[0](state,fert) *= fert_factor;
  }

  //compute fert_trace and best_prev_score
  for (uint state = 0; state <= start_allfert_max_reachable_state; state++) {

    long double best_score = 0.0;
    uchar best_fert = 255;
    
    for (uint fert=0; fert <= maxFertility; fert++) {
      const long double cur_score = score[0](state,fert);

      if (cur_score > best_score) {
	best_score = cur_score;
	best_fert = fert;
      }
    }

    best_prev_score[state] = best_score;
    fert_trace[0][state] = best_fert;
  }

  /**** now proceeed with the remainder of the sentence ****/

  for (uint i=1; i < curI; i++) {
    std::cerr << "********* i: " << i << " ***************" << std::endl;

    const uint ti = cur_target[i];
    const Math1D::Vector<double>& cur_dict = dict_[ti];

    Math1D::NamedVector<long double> translation_cost(curJ+1,MAKENAME(translation_cost));
    translation_cost[0] = 0.0; //we do not allow an empty word here
    for (uint j=1; j <= curJ; j++) {
      translation_cost[j] = cur_dict[cur_lookup(j-1,i)] * cur_distort_prob(j-1,i);
    }

    const uint allfert_max_reachable_j  = std::min(curJ, nMaxSkips + (i+1)*maxFertility);
    const uint fertone_max_reachable_j  = std::min(curJ, nMaxSkips + i*maxFertility+1);
    const uint prevword_max_reachable_j = std::min(curJ, nMaxSkips + i*maxFertility);
    
    const uint prevword_max_reachable_state = first_state_[prevword_max_reachable_j+1]-1;
    const uint allfert_max_reachable_state = first_state_[allfert_max_reachable_j+1]-1;
    const uint fertone_max_reachable_state = first_state_[fertone_max_reachable_j+1]-1;

    fert_trace[i].set_name("fert_trace["+toString(i)+"]");
    fert_trace[i].resize(allfert_max_reachable_state+1,255);

    state_trace[i].set_name("state_trace["+toString(i)+"]");
    state_trace[i].resize(allfert_max_reachable_state+1,maxFertility,255);

    score[i-1].resize(0,0);
    score[i].set_name("score["+toString(i)+"]");
    
    Math2D::NamedMatrix<long double>& cur_score = score[i];
    cur_score.resize(nStates,maxFertility+1,0.0);

    Math2D::NamedMatrix<uchar>& cur_state_trace = state_trace[i];    

//     std::cerr << "fert 0" << std::endl;
//     std::cerr << "prevword_max_reachable_state: " << prevword_max_reachable_state << std::endl;
//     std::cerr << "allfert_max_reachable_state: " << allfert_max_reachable_state << std::endl;

    //fertility 0
    for (uint state=0; state <= prevword_max_reachable_state; state++) {
      cur_score(state,0) = best_prev_score[state];
    }

    //std::cerr << " fert 1" << std::endl; 

    //fertility 1
    for (uint state=0; state <= fertone_max_reachable_state; state++) {

      assert(coverage_state_(1,state) <= curJ);

      long double best_score = 0.0;
      uchar trace_entry = 255;
	
      const uint nPredecessors = predecessor_coverage_states_[state].yDim();
      assert(nPredecessors < 255);	
      
      for (uint p=0; p < nPredecessors; p++) {
	
	const uint prev_state = predecessor_coverage_states_[state](0,p);
	const uint cover_j = predecessor_coverage_states_[state](1,p);
	
	assert(cover_j <= curJ);
	
	const long double hyp_score = best_prev_score[prev_state] * translation_cost[cover_j];
	
	if (hyp_score > best_score) {
	  best_score = hyp_score;
	  trace_entry = p;
	}
      }
      
      cur_score(state,1) = best_score;
      cur_state_trace(state,0) = trace_entry;
    }

    //fertility > 1
    for (uint fert=2; fert <= maxFertility; fert++) {

      const uint curfert_max_reachable_j  = std::min(curJ, nMaxSkips + i*maxFertility + fert);
      const uint curfert_max_reachable_state = first_state_[curfert_max_reachable_j+1]-1;

      //std::cerr << "fert: " << fert << std::endl;

      for (uint state=0; state <= curfert_max_reachable_state; state++) {

	assert(coverage_state_(1,state) <= curJ);

	long double best_score = 0.0;
	uchar trace_entry = 255;
	  
	const uint nPredecessors = predecessor_coverage_states_[state].yDim();
	assert(nPredecessors < 255);
	
	for (uint p=0; p < nPredecessors; p++) {
	  
	  const uint prev_state = predecessor_coverage_states_[state](0,p);
	  const uint cover_j = predecessor_coverage_states_[state](1,p);
	  
	  assert(cover_j <= curJ);
  
	  const long double hyp_score = cur_score(prev_state,fert-1) * translation_cost[cover_j];
	  
	  if (hyp_score > best_score) {
	    best_score = hyp_score;
	    trace_entry = p;
	  }
	}
	
	cur_score(state,fert) = best_score;
	cur_state_trace(state,fert-1) = trace_entry;
      }
    }

    //std::cerr << "including fertility probs" << std::endl;

    //include fertility probabilities
    for (uint fert = 0; fert <= maxFertility; fert++) {
      long double fert_factor = (fertility_prob_[ti].size() > fert) ? fertility_prob_[ti][fert] : 0.0;
      if (fert > 1)
	fert_factor *= ldfac(fert);
      
      for (uint state=0; state <= allfert_max_reachable_state; state++) 
	cur_score(state,fert) *= fert_factor;
    }

    //DEBUG
    //best_prev_score.set_constant(0.0);
    //END_DEBUG

    //compute fert_trace and best_prev_score
    for (uint state=0; state <= allfert_max_reachable_state; state++) {

      long double best_score = 0.0;
      uchar best_fert = 255;
      
      for (uint fert=0; fert <= maxFertility; fert++) {
	const long double cand_score = cur_score(state,fert);
	
	if (cand_score > best_score) {
	  best_score = cand_score;
	  best_fert = fert;
	}
      }
      
      best_prev_score[state] = best_score;
      fert_trace[i][state] = best_fert;
    }
  }

  uint end_state = first_state_[curJ];
  assert(coverage_state_(0,end_state) == 0);
  assert(coverage_state_(1,end_state) == curJ);
  long double best_score = best_prev_score[end_state];
  ushort best_end_fert = fert_trace[curI-1][end_state];

  /**** traceback ****/
  best_known_alignment_[s].set_constant(0);

  uint fert = best_end_fert;
  uint i = curI-1;
  uint state = end_state;

  while (true) {

//     std::cerr << "**** traceback: i=" << i << ", fert=" << fert << ", state #" << state
// 	      << ", uncovered set=";
//     print_uncovered_set(coverage_state_(0,state));
//     std::cerr << " ; max_covered_j=" << coverage_state_(1,state) << std::endl;

    //std::cerr << "score: " << score[i](state,fert) << std::endl;

    //default values apply to the case with fertility 0
    uint prev_i = i-1;
    uint prev_state = state;

    if (fert > 0) {
      
      uint covered_j = coverage_state_(1,state);

      if (i > 0 || fert > 1) {
	const uchar transition = state_trace[i](state,fert-1);
	assert(transition != 255);
	
	prev_state = predecessor_coverage_states_[state](0,transition);
	covered_j = predecessor_coverage_states_[state](1,transition);
      }

      //std::cerr << "covered j: " << covered_j << std::endl;

      best_known_alignment_[s][covered_j-1] = i+1;
    
      if (fert > 1)
	prev_i = i;
    }

    if (i == 0 && fert <= 1)
      break;

    //default value applies to the case with fertility > 1
    uint prev_fert = fert-1;    

    if (fert <= 1)
      prev_fert = fert_trace[prev_i][prev_state];

    fert = prev_fert;
    i = prev_i;
    state = prev_state;
  }

  return best_score;
}

#ifdef HAS_CBC
class IBM3IPHeuristic : public CbcHeuristic {
public:

  IBM3IPHeuristic(CbcModel& model, uint I, uint J, uint nFertilityVarsPerWord);

  IBM3IPHeuristic(const CbcHeuristic& heuristic, CbcModel& model, 
		  uint I, uint J, uint nFertilityVarsPerWord);


  virtual CbcHeuristic* clone() const;

  virtual void resetModel(CbcModel* model);

  virtual int solution(double& objectiveValue, double* newSolution);

protected:

  uint I_;
  uint J_;
  uint nFertilityVarsPerWord_;
};


IBM3IPHeuristic::IBM3IPHeuristic(CbcModel& model, uint I, uint J, uint nFertilityVarsPerWord) :
  CbcHeuristic(model), I_(I), J_(J), nFertilityVarsPerWord_(nFertilityVarsPerWord)
{}

IBM3IPHeuristic::IBM3IPHeuristic(const CbcHeuristic& heuristic, CbcModel& model, uint I, uint J, uint nFertilityVarsPerWord) :
  CbcHeuristic(heuristic), I_(I), J_(J), nFertilityVarsPerWord_(nFertilityVarsPerWord)
{}

/*virtual*/ CbcHeuristic* IBM3IPHeuristic::clone() const {
  return new IBM3IPHeuristic(*this,*model_,I_,J_,nFertilityVarsPerWord_);
}

/*virtual*/ void IBM3IPHeuristic::resetModel(CbcModel* model) {
  TODO("resetModel");
}

/*virtual*/ int IBM3IPHeuristic::solution(double& objectiveValue, double* newSolution) {

  uint nVars = (I_+1)*J_ + (I_+1)*nFertilityVarsPerWord_;

  for (uint v=0; v < nVars; v++) {
    newSolution[v] = 0.0;
  }

  OsiSolverInterface* solver = model_->solver();
  const double* cur_solution = solver->getColSolution();

  Math1D::NamedVector<uint> fert_count(I_+1,0,MAKENAME(fert_count));

  for (uint j=0; j < J_; j++) {

    double max_var = 0.0;
    uint aj = MAX_UINT;

    for (uint i=0; i <= I_; i++) {

      double var_val = cur_solution[j*(I_+1)+i];

      if (var_val > max_var) {
	max_var = var_val;
	aj = i;
      }
    }

    newSolution[j*(I_+1)+aj] = 1.0;

    fert_count[aj]++;
  }
  
  uint fert_var_offs = J_*(I_+1);

  for (uint i=0; i <= I_; i++) {

    newSolution[fert_var_offs + i*nFertilityVarsPerWord_ + fert_count[i]] = 1.0;
  }


  uint return_code = 0;

  const double* objective = solver->getObjCoefficients();

  double new_energy = 0.0;
  for (uint v=0; v < nVars; v++)
    new_energy += newSolution[v] * objective[v];


  if (new_energy < objectiveValue) {

    return_code = 1;
  }

  //std::cerr << "new integer energy " << new_energy << ",  previous " << objectiveValue << std::endl;

  return return_code;
}
#endif

long double IBM3Trainer::compute_viterbi_alignment_ilp(uint s, uint max_fertility,
						       Math1D::Vector<ushort>& alignment, double time_limit) {

#ifdef HAS_CBC
  const Storage1D<uint>& cur_source = source_sentence_[s];
  const Storage1D<uint>& cur_target = target_sentence_[s];
  const Math2D::Matrix<uint>& cur_lookup = slookup_[s];
  
  const uint curI = cur_target.size();
  const uint curJ = cur_source.size();
  const Math2D::Matrix<double>& cur_distort_prob = distortion_prob_[curJ-1];

  uint nFertVarsPerWord = std::min(curJ,max_fertility) + 1;

  std::cerr << "computing lp-relax for sentence pair #" << s << ", I= " << curI << ", J= " << curJ << std::endl;

  uint nVars = (curI+1)*curJ  //alignment variables
    + (curI+1)*nFertVarsPerWord; //fertility variables
  
  uint fert_var_offs = (curI+1)*curJ;
  //std::cerr << "fert_var_offs: " << fert_var_offs << std::endl;

  uint nConstraints = curJ // alignment variables must sum to 1 for each source word
    + curI + 1 //fertility variables must sum to 1 for each target word including the empty word
    + curI + 1; //fertility variables must be consistent with alignment variables

  uint fert_con_offs = curJ;
  uint consistency_con_offs = fert_con_offs + curI + 1;
  
  Math1D::NamedVector<double> cost(nVars,0.0,MAKENAME(cost));

  Math1D::NamedVector<double> var_lb(nVars,0.0,MAKENAME(var_lb));
  Math1D::NamedVector<double> var_ub(nVars,1.0,MAKENAME(var_ub));

  Math1D::NamedVector<double> jcost_lower_bound(curJ,MAKENAME(jcost_lower_bound));
  Math1D::NamedVector<double> icost_lower_bound(curI+1,MAKENAME(icost_lower_bound));

  //code cost entries for alignment variables
  for (uint j=0; j < curJ; j++) {

    uint s_idx = cur_source[j];
    const uint cur_offs = j*(curI+1);

    double min_cost = 1e50;

    for (uint i=0; i <= curI; i++) {

      if (i == 0) {
	
	cost[cur_offs] = - logl(dict_[0][s_idx-1]); //distortion is here handled by the fertilities
	assert(!isnan(cost[cur_offs]));
	
      }
      else {
	const uint ti = cur_target[i-1];
	
	cost[cur_offs + i] = -logl(dict_[ti][cur_lookup(j,i-1) ]) - logl( cur_distort_prob(j,i-1) );
	assert(!isnan(cost[cur_offs+i]));
      }

      if (cost[cur_offs + i] < min_cost)
	min_cost = cost[cur_offs + i];
    }

    jcost_lower_bound[j] = min_cost;
  }
  
  //code cost entries for the fertility variables of the empty word
  double min_empty_fert_cost = 1e50;
  for (uint fert=0; fert < nFertVarsPerWord; fert++) {

    if (curJ-fert >= fert) {
      long double prob = 1.0;
      
      prob *= ldchoose(curJ-fert,fert);
      for (uint k=1; k <= fert; k++)
	prob *= p_zero_;
      for (uint k=1; k <= curJ-2*fert; k++)
	prob *= p_nonzero_;
    
      if (och_ney_empty_word_) {
	
	for (uint k=1; k<= fert; k++)
	  prob *= ((long double) k) / curJ;
      }
      
      if (prob >= 1e-300) {
	cost[fert_var_offs + fert] = - logl( prob );
	assert(!isnan(cost[fert_var_offs+fert]));
      }
      else {
	cost[fert_var_offs + fert] = 1e10;
      }

      if (cost[fert_var_offs + fert] < min_empty_fert_cost) {
	min_empty_fert_cost = cost[fert_var_offs + fert];
      }
    }
    else {      
      cost[fert_var_offs + fert] = 1e10;
      var_ub[fert_var_offs + fert] = 0.0;
    }
  }
  icost_lower_bound[0] = min_empty_fert_cost;

  //code cost entries for the fertility variables of the real words
  for (uint i=0; i < curI; i++) {

    const uint ti = cur_target[i];

    double min_cost = 1e50;

    for (uint fert=0; fert < nFertVarsPerWord; fert++) {

      uint idx = fert_var_offs + (i+1)*nFertVarsPerWord + fert;

      if (fertility_prob_[ti][fert] > 1e-75) {
	cost[idx] = -logl( ldfac(fert) * fertility_prob_[ti][fert]  );
	assert(!isnan(cost[fert_var_offs + (i+1)*nFertVarsPerWord + fert]));
      }
      else 
	cost[idx] = 1e10;

      if (cost[idx] < min_cost)
	min_cost = cost[idx];
    }

    icost_lower_bound[i+1] = min_cost;
  }

  Math2D::NamedMatrix<double> ifert_cost(curJ+1,curI+1,1e50,MAKENAME(ifert_cost));
  
  for (uint f=0; f < nFertVarsPerWord; f++) {
    ifert_cost(f,0) = cost[fert_var_offs + f];
  }

  for (uint i = 1; i <= curI; i++) {

    for (uint j=0; j <= curJ; j++) {

      double opt_cost = ifert_cost(j,i-1) + cost[fert_var_offs+i*nFertVarsPerWord];

      for (uint f=1; f < std::min(j+1,nFertVarsPerWord); f++) {
	
	double hyp_cost = ifert_cost(j-f,i-1) + cost[fert_var_offs+i*nFertVarsPerWord + f];

	if (hyp_cost < opt_cost) {
	  opt_cost = hyp_cost;
	}
      }

      ifert_cost(j,i) = opt_cost;
    }
  }

  uint nHighCost = 0;

  double upper_bound = -logl(alignment_prob(s,best_known_alignment_[s]));
  double lower_bound = jcost_lower_bound.sum() + ifert_cost(curJ,curI);
  
  double loose_lower_bound = jcost_lower_bound.sum() + icost_lower_bound.sum();

  //std::cerr << "lower bound: " << lower_bound << " = " << jcost_lower_bound.sum() << " + " 
  //    << ifert_cost(curJ,curI) << std::endl;
  
  double gap = upper_bound - lower_bound;
  double loose_gap = upper_bound - loose_lower_bound;

  double loose_bound2 = 0.0;
  Math1D::Vector<double> values(curJ);
  Math1D::Vector<double> ibound2(curI+1);
  Math2D::Matrix<double> approx_icost(curI+1,nFertVarsPerWord);

  for (uint i=0; i <= curI; i++) {

    for (uint j=0; j < curJ; j++)
      values[j] = cost[j*(curI+1)+i] - jcost_lower_bound[j];

    std::sort(values.direct_access(),values.direct_access()+curJ);

    for (uint j=1; j < curJ; j++)
      values[j] += values[j-1];
   
    approx_icost(i,0) = cost[fert_var_offs+i*nFertVarsPerWord];

    double min_cost = approx_icost(i,0);

    for (uint f=1; f < nFertVarsPerWord; f++) {
      //for (uint f=1; f < cur_limit; f++) {

      //approx_icost(i,f) = cost[fert_var_offs+i*nFertVarsPerWord+f] + values[f-1];
      if (i == 0)
	approx_icost(i,f) = cost[fert_var_offs+f] + values[f-1];
      else
	approx_icost(i,f) = cost[fert_var_offs + nFertVarsPerWord + (i-1)*nFertVarsPerWord + f] + values[f-1];

      if (approx_icost(i,f) < min_cost)
	min_cost = approx_icost(i,f);
    }
    ibound2[i] = min_cost;

    loose_bound2 += min_cost;
  }

  double loose_gap2 = upper_bound + 0.1 - loose_bound2;  

#if 1
  for (uint j=0; j < curJ; j++) {
    
    for (uint aj=0; aj <= curI; aj++) {
      if (cost[j*(curI+1) + aj] >= gap+jcost_lower_bound[j]+0.1) {

	var_ub[j*(curI+1) + aj] = 0.0;
	nHighCost++;
      }
    }
  }

  for (uint i=0; i <= curI; i++) {

    for (uint f=0; f < nFertVarsPerWord; f++) {
      
      if (cost[fert_var_offs+i*nFertVarsPerWord + f] >= icost_lower_bound[i] + loose_gap + 0.1) {
 	var_ub[fert_var_offs+i*nFertVarsPerWord + f] = 0.0;
 	nHighCost++;
      }
      else if (approx_icost(i,f) >= loose_gap2 + ibound2[i]) {
	var_ub[fert_var_offs+i*nFertVarsPerWord + f] = 0.0;
 	nHighCost++;
      }
    }
  }
#endif


  for (uint v=0; v < nVars; v++) {

    assert(!isnan(cost[v]));

    if (cost[v] > 1e10) {
      nHighCost++;
      cost[v] = 1e10;
      var_ub[v] = 0.0;
    }
  }

  //if (nHighCost > 0) //std::cerr << "WARNING: dampened " << nHighCost << " cost entries" << std::endl;

//   std::cerr << "highest cost: " << cost.max() << std::endl;

  Math1D::NamedVector<double> rhs(nConstraints,1.0,MAKENAME(rhs));
  
  for (uint c=consistency_con_offs; c < nConstraints; c++)
    rhs[c] = 0.0;

  //code matrix constraints
  uint nMatrixEntries = (curI+1)*curJ // for the alignment unity constraints
    + (curI+1)*(nFertVarsPerWord+1) // for the fertility unity constraints
    + (curI+1)*(nFertVarsPerWord-1+curJ); // for the consistency constraints

  SparseMatrixDescription<double> lp_descr(nMatrixEntries, nConstraints, nVars);

  //code unity constraints for alignment variables
  for (uint j=0; j < curJ; j++) {
  
    for (uint v= j*(curI+1); v < (j+1)*(curI+1); v++) {
      if (var_ub[v] > 0.0)
	lp_descr.add_entry(j, v, 1.0);
    }
  }


  //code unity constraints for fertility variables
  for (uint i=0; i <= curI; i++) {

    for (uint fert=0; fert < nFertVarsPerWord; fert++) {

      lp_descr.add_entry(fert_con_offs + i, fert_var_offs + i*nFertVarsPerWord + fert, 1.0 );
    }
  }

  //code consistency constraints
  for (uint i=0; i <= curI; i++) {

    uint row = consistency_con_offs + i;

    for (uint fert=1; fert < nFertVarsPerWord; fert++) {

      uint col = fert_var_offs + i*nFertVarsPerWord + fert;

      lp_descr.add_entry(row, col, fert);

    }

    for (uint j=0; j < curJ; j++) {

      uint col = j*(curI+1) + i;

      if (var_ub[col] > 0.0)
	lp_descr.add_entry(row, col, -1.0);
    }
  }

  CoinPackedMatrix coinMatrix(false,(int*) lp_descr.row_indices(),(int*) lp_descr.col_indices(),
			      lp_descr.value(),lp_descr.nEntries());

  OsiClpSolverInterface clp_interface;

  clp_interface.setLogLevel(0);
  clp_interface.messageHandler()->setLogLevel(0);

  clp_interface.loadProblem (coinMatrix, var_lb.direct_access(), var_ub.direct_access(),   
			     cost.direct_access(), rhs.direct_access(), rhs.direct_access());

  for (uint v=0 /*fert_var_offs*/; v < nVars; v++) {
    clp_interface.setInteger(v);
  }

  timeval tStartCLP, tEndCLP;
  
  gettimeofday(&tStartCLP,0);

  int error = 0; 
  clp_interface.resolve();
  error =  1 - clp_interface.isProvenOptimal();

  if (error) {
    INTERNAL_ERROR << "solving the LP-relaxation failed. Exiting..." << std::endl;
    exit(1);
  }

  gettimeofday(&tEndCLP,0);
  //std::cerr << "CLP-time: " << diff_seconds(tEndCLP,tStartCLP) << " seconds. " << std::endl;

  const double* lp_solution = clp_interface.getColSolution(); 
  long double energy = 0.0;

  uint nNonIntegral = 0;
  uint nNonIntegralFert = 0;

  for (uint v=0; v < nVars; v++) {

    double var_val = lp_solution[v];

    if (var_val > 0.01 && var_val < 0.99) {
      nNonIntegral++;

      if (v >= fert_var_offs)
	nNonIntegralFert++;
    }

    energy += cost[v] * var_val;
  }

  //std::cerr << nNonIntegral << " non-integral variables (" << (nNonIntegral - nNonIntegralFert)  
  //    << "/" << nNonIntegralFert << ")" << std::endl;

  const double* solution = lp_solution;

  CbcModel cbc_model(clp_interface);

  if (nNonIntegral > 0) {

    //std::cerr << "lp-relax for sentence pair #" << s << ", I= " << curI << ", J= " << curJ << std::endl;

    //clp_interface.findIntegersAndSOS(false);
    clp_interface.setupForRepeatedUse();
    
    //if (curI <= 30 || curJ <= 30) 
    cbc_model.messageHandler()->setLogLevel(0);
    cbc_model.setLogLevel(0);

    if (time_limit > 0.0)
      cbc_model.setMaximumSeconds(time_limit);

    CglGomory gomory_cut;
    gomory_cut.setLimit(500);
    gomory_cut.setAway(0.01);
    gomory_cut.setLimitAtRoot(500);
    gomory_cut.setAwayAtRoot(0.01);
    cbc_model.addCutGenerator(&gomory_cut,0,"Gomory Cut");

    CglProbing probing_cut;
    probing_cut.setUsingObjective(true);
    probing_cut.setMaxPass(10);
    probing_cut.setMaxPassRoot(50);
    probing_cut.setMaxProbe(100);
    probing_cut.setMaxProbeRoot(500);
    probing_cut.setMaxLook(150);
    probing_cut.setMaxLookRoot(1500);
    //cbc_model.addCutGenerator(&probing_cut,0,"Probing Cut");
    
    CglRedSplit redsplit_cut;
    redsplit_cut.setLimit(1500);
    //cbc_model.addCutGenerator(&redsplit_cut,0,"RedSplit Cut");

    //CglMixedIntegerRounding mi1_cut;
    //cbc_model.addCutGenerator(&mi1_cut,0,"Mixed Integer Cut 1");

    CglMixedIntegerRounding2 mi2_cut;
    //cbc_model.addCutGenerator(&mi2_cut,0,"Mixed Integer 2");

    CglTwomir twomir_cut;
    //cbc_model.addCutGenerator(&twomir_cut,0,"Twomir Cut");

    CglLandP landp_cut;
    //cbc_model.addCutGenerator(&landp_cut,0,"LandP Cut");

    CglOddHole oddhole_cut;
    //cbc_model.addCutGenerator(&oddhole_cut,0,"OddHole Cut");

    //CglClique clique_cut;
    //cbc_model.addCutGenerator(&clique_cut,0,"Clique Cut");

    //CglStored stored_cut;
    //cbc_model.addCutGenerator(&stored_cut,0,"Stored Cut");
    
    IBM3IPHeuristic  ibm3_heuristic(cbc_model, curI, curJ, nFertVarsPerWord);
    ibm3_heuristic.setWhereFrom(63);
    cbc_model.addHeuristic(&ibm3_heuristic,"IBM3 Heuristic");

    /*** set initial upper bound given by best_known_alignment_[s] ****/
    Math1D::Vector<double> best_sol(nVars,0.0);
    Math1D::Vector<uint> fert_count(curI+1,0);
    
    for (uint j=0; j < curJ; j++) {
      
      uint aj = best_known_alignment_[s][j];
      fert_count[aj]++;
      best_sol[ j*(curI+1)+aj] = 1.0;
    } 
    for (uint i=0; i <= curI; i++) {
      best_sol[fert_var_offs + i*nFertVarsPerWord + fert_count[i] ] = 1.0;
    }
    
    double temp_energy = 0.0;
    for (uint v=0; v < nVars; v++)
      temp_energy += cost[v]*best_sol[v];

    cbc_model.setBestSolution(best_sol.direct_access(),nVars,temp_energy,true);

    cbc_model.branchAndBound();

    const double* cbc_solution = cbc_model.bestSolution();

    energy = 0.0;

    for (uint v=0; v < nVars; v++) {
      
      double var_val = cbc_solution[v];
      
      energy += cost[v] * var_val;
    }

    solution = cbc_solution;
  }

  alignment.resize(curJ);

  uint nNonIntegralVars = 0;

  Math1D::Vector<uint> fert(curI+1,0);

  for (uint j=0; j < curJ; j++) {

    double max_val = 0.0;
    uint arg_max = MAX_UINT;
    
    uint nNonIntegralVars = 0;

    for (uint i=0; i <= curI; i++) {

      double val = solution[j*(curI+1)+i];
      
      if (val > 0.01 && val < 0.99)
	nNonIntegralVars++;

      if (val > max_val) {

	max_val = val;
	arg_max = i;
      }
    }

    alignment[j] = arg_max;
    fert[arg_max]++;
  }

  //std::cerr << nNonIntegralVars << " non-integral variables after branch and cut" << std::endl;
  
  long double clp_prob =  expl(-energy);
  long double actual_prob = alignment_prob(s,alignment);

  //std::cerr << "clp alignment: " << clp_alignment << std::endl;
  //std::cerr << "clp-prob:    " << clp_prob << std::endl;
  //std::cerr << "direct prob: " << direct_prob << std::endl;
  //std::cerr << "actual prob:      " << actual_prob << std::endl;

  return actual_prob;
#else
  alignment = best_known_alignment_[s];
  return alignment_prob(s,alignment);
#endif
}

void IBM3Trainer::train_with_ibm_constraints(uint nIter, uint maxFertility, uint nMaxSkips, bool verbose) {


  ReducedIBM3DistortionModel fdistort_count(distortion_prob_.size(),MAKENAME(fdistort_count));
  for (uint J=0; J < fdistort_count.size(); J++) {
    fdistort_count[J].resize_dirty(distortion_prob_[J].xDim(), distortion_prob_[J].yDim());
  }

  const uint nTargetWords = dict_.size();

  NamedStorage1D<Math1D::Vector<uint> > fwcount(nTargetWords,MAKENAME(fwcount));
  NamedStorage1D<Math1D::Vector<double> > ffert_count(nTargetWords,MAKENAME(ffert_count));

  for (uint i=0; i < nTargetWords; i++) {
    fwcount[i].resize(dict_[i].size());
    ffert_count[i].resize_dirty(fertility_prob_[i].size());
  }

  long double fzero_count;
  long double fnonzero_count;


  compute_uncovered_sets(nMaxSkips); 
  compute_coverage_states();

  for (uint iter=1; iter <= nIter; iter++) {

    fzero_count = 0.0;
    fnonzero_count = 0.0;

    for (uint i=0; i < nTargetWords; i++) {
      fwcount[i].set_constant(0.0);
      ffert_count[i].set_constant(0.0);
    }

    for (uint J=0; J < distortion_prob_.size(); J++) {
      fdistort_count[J].set_constant(0.0);
    }

    double max_perplexity = 0.0;

    uint nBetter = 0;
    uint nEqual = 0;

    for (uint s=0; s < source_sentence_.size(); s++) {

      long double prev_prob = (verbose) ? alignment_prob(s,best_known_alignment_[s]) : 0.0;

      long double prob = compute_ibmconstrained_viterbi_alignment_noemptyword(s,maxFertility,nMaxSkips);
      prob *= pow(p_nonzero_,source_sentence_[s].size());
      std::cerr << "probability " << prob << std::endl;
      std::cerr << "generated alignment: " << best_known_alignment_[s] << std::endl;

      long double check_prob = alignment_prob(s,best_known_alignment_[s]);
      double check_ratio = prob / check_prob;
      std::cerr << "check_ratio: " << check_ratio << std::endl;
      assert(check_ratio > 0.999 && check_ratio < 1.001);

      if (verbose) {
	if (prev_prob == check_prob)
	  nEqual++;
	else if (prev_prob < check_prob)
	  nBetter++;
      }

      max_perplexity -= std::log(prob);

      const Storage1D<uint>&  cur_source = source_sentence_[s];
      const Storage1D<uint>&  cur_target = target_sentence_[s];
      const Math2D::Matrix<uint>& cur_lookup = slookup_[s];      

      const uint curJ = source_sentence_[s].size();
      const uint curI = target_sentence_[s].size();
 
      Math2D::Matrix<double>& cur_distort_count = fdistort_count[curJ-1];

      Math1D::Vector<uint> fertility(curI+1,0);
      for (uint j=0; j < curJ; j++) {
	fertility[best_known_alignment_[s][j]]++;
      }

      //currently implementing Viterbi training

      double cur_zero_weight = 1.0;
      cur_zero_weight /= (curJ - fertility[0]);
      
      fzero_count += cur_zero_weight * (fertility[0]);
      fnonzero_count += cur_zero_weight * (curJ - 2*fertility[0]);

      //increase counts for dictionary and distortion
      for (uint j=0; j < curJ; j++) {

	const uint s_idx = cur_source[j];
	const uint cur_aj = best_known_alignment_[s][j];

	if (cur_aj != 0) {
	  fwcount[cur_target[cur_aj-1]][cur_lookup(j,cur_aj-1)] += 1;
	  cur_distort_count(j,cur_aj-1) += 1.0;
	  assert(!isnan(cur_distort_count(j,cur_aj-1)));
	}
	else {
	  fwcount[0][s_idx-1] += 1;
	}
      }

      //update fertility counts
      for (uint i=1; i <= curI; i++) {

	const uint cur_fert = fertility[i];
	const uint t_idx = cur_target[i-1];

	ffert_count[t_idx][cur_fert] += 1.0;
      }
    }


    //update p_zero_ and p_nonzero_
    double fsum = fzero_count + fnonzero_count;
    p_zero_ = fzero_count / fsum;
    p_nonzero_ = fnonzero_count / fsum;

    std::cerr << "new p_zero: " << p_zero_ << std::endl;

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

    //update distortion prob from counts
    for (uint J=0; J < distortion_prob_.size(); J++) {

//       std::cerr << "J:" << J << std::endl;
//       std::cerr << "distort_count: " << fdistort_count[J] << std::endl;
      for (uint i=0; i < distortion_prob_[J].yDim(); i++) {

	double sum = 0.0;
	for (uint j=0; j < J+1; j++)
	  sum += fdistort_count[J](j,i);

	if (sum > 1e-305) {
	  const double inv_sum = 1.0 / sum;
	  assert(!isnan(inv_sum));
	  
	  for (uint j=0; j < J+1; j++) {
	    distortion_prob_[J](j,i) = inv_sum * fdistort_count[J](j,i);
	    if (isnan(distortion_prob_[J](j,i))) {
	      std::cerr << "sum: " << sum << std::endl;
	      std::cerr << "set to " << inv_sum << " * " << fdistort_count[J](j,i) << " = "
			<< (inv_sum * fdistort_count[J](j,i)) << std::endl;
	    }
	    assert(!isnan(fdistort_count[J](j,i)));
	    assert(!isnan(distortion_prob_[J](j,i)));
	  }
	}
	else {
	  //std::cerr << "WARNING: did not update distortion count because sum was " << sum << std::endl;
	}
      }
    }

    for (uint i=1; i < nTargetWords; i++) {

      //std::cerr << "i: " << i << std::endl;

      const double sum = ffert_count[i].sum();

      if (sum > 1e-305) {

	if (fertility_prob_[i].size() > 0) {
	  assert(sum > 0.0);     
	  const double inv_sum = 1.0 / sum;
	  assert(!isnan(inv_sum));
	  
	  for (uint f=0; f < fertility_prob_[i].size(); f++)
	    fertility_prob_[i][f] = inv_sum * ffert_count[i][f];
	}
	else {
	  std::cerr << "WARNING: target word #" << i << " does not occur" << std::endl;
	}
      }
      else {
	std::cerr << "WARNING: did not update fertility count because sum was " << sum << std::endl;
      }
    }
    
    if (possible_ref_alignments_.size() > 0) {
      
      std::cerr << "#### IBM3-AER in between iterations #" << (iter-1) << " and " << iter << ": " << AER() << std::endl;
      std::cerr << "#### IBM3-fmeasure in between iterations #" << (iter-1) << " and " << iter << ": " << f_measure() << std::endl;
      std::cerr << "#### IBM3-DAE/S in between iterations #" << (iter-1) << " and " << iter << ": " 
		<< DAE_S() << std::endl;
    }

    if (verbose) {
      std::cerr << "ibm-coinstraints are eqaul to hillclimbing in " << nEqual << " cases" << std::endl;
      std::cerr << "ibm-coinstraints are better than hillclimbing in " << nBetter << " cases" << std::endl;
    }    

    max_perplexity /= source_sentence_.size();

    std::cerr << "max-fertility after iteration #" << (iter - 1) << ": " << max_perplexity << std::endl;
  }
}

/************************************************** implementation of IBM4Trainer ***********************************************/

IBM4Trainer::IBM4Trainer(const Storage1D<Storage1D<uint> >& source_sentence,
			 const Storage1D<Math2D::Matrix<uint> >& slookup,
			 const Storage1D<Storage1D<uint> >& target_sentence,
			 const std::map<uint,std::set<std::pair<uint,uint> > >& sure_ref_alignments,
			 const std::map<uint,std::set<std::pair<uint,uint> > >& possible_ref_alignments,
			 SingleWordDictionary& dict,
			 const CooccuringWordsType& wcooc,
			 uint nSourceWords, uint nTargetWords,
			 const floatSingleWordDictionary& prior_weight,
			 bool och_ney_empty_word,
			 bool use_sentence_start_prob,
			 bool no_factorial,
			 IBM4CeptStartMode cept_start_mode)
  : FertilityModelTrainer(source_sentence,slookup,target_sentence,dict,wcooc,
			  nSourceWords,nTargetWords,sure_ref_alignments,possible_ref_alignments),
    cept_start_prob_(1,1,2*maxJ_-1,MAKENAME(cept_start_prob_)),
    within_cept_prob_(1,2*maxJ_-1,MAKENAME(within_cept_prob_)), 
    sentence_start_prob_(MAKENAME(sentence_start_prob)),
    och_ney_empty_word_(och_ney_empty_word), cept_start_mode_(cept_start_mode),
    use_sentence_start_prob_(use_sentence_start_prob), no_factorial_(no_factorial), prior_weight_(prior_weight)
 {

  const uint nDisplacements = 2*maxJ_-1;
  displacement_offset_ = maxJ_-1;

  cept_start_prob_.set_constant(1.0 / nDisplacements);
  within_cept_prob_.set_constant(1.0 / (nDisplacements-1));
  for (uint x=0; x < within_cept_prob_.xDim(); x++)
    within_cept_prob_(x,displacement_offset_) = 0.0;  //within cepts displacements of zero are impossible

  if (use_sentence_start_prob_) {
    sentence_start_prob_.resize(maxJ_, 1.0 / maxJ_);
  }
}

void IBM4Trainer::init_from_ibm3(IBM3Trainer& ibm3trainer) {

  std::cerr << "******** initializing IBM4 from IBM3 *******" << std::endl;
  
  fertility_prob_.resize(ibm3trainer.fertility_prob().size());
  for (uint k=0; k < fertility_prob_.size(); k++)
    fertility_prob_[k] = ibm3trainer.fertility_prob()[k];

  for (uint s=0; s < source_sentence_.size(); s++) 
    best_known_alignment_[s] = ibm3trainer.best_alignments()[s];

  p_zero_ = ibm3trainer.p_zero();
  p_nonzero_ = 1.0 - p_zero_;

  //init distortion models from best known alignments
  cept_start_prob_.set_constant(0.0);
  within_cept_prob_.set_constant(0.0);
  sentence_start_prob_.set_constant(0.0);
 
  for (uint s=0; s < source_sentence_.size(); s++) {

    const uint curI = target_sentence_[s].size();
    const uint curJ = source_sentence_[s].size();

    NamedStorage1D<std::set<uint> > aligned_source_words(curI+1,MAKENAME(aligned_source_words));
    
    for (uint j=0; j < curJ; j++) {
      const uint aj = best_known_alignment_[s][j];
      aligned_source_words[aj].insert(j);
    }

    uint prev_center = MAX_UINT;
    
    for (uint i=1; i <= curI; i++) {
      
      if (!aligned_source_words[i].empty()) {
	
	double sum_j = 0;
	uint nAlignedWords = 0;

	std::set<uint>::iterator ait = aligned_source_words[i].begin();
	const uint first_j = *ait;
	sum_j += first_j;
	nAlignedWords++;
		
	//collect counts for the head model
	if (prev_center != MAX_UINT) {
	  int diff =  first_j - prev_center;
	  diff += displacement_offset_;
	  cept_start_prob_(0,0,diff) += 1.0;
	}
	else if (use_sentence_start_prob_)
	  sentence_start_prob_[first_j] += 1.0;
		
	//collect counts for the within-cept model
	uint prev_j = first_j;
	for (++ait; ait != aligned_source_words[i].end(); ait++) {
	  
	  const uint cur_j = *ait;
	  sum_j += cur_j;
	  nAlignedWords++;
	  
	  int diff = cur_j - prev_j;
	  diff += displacement_offset_;
	  within_cept_prob_(0,diff) += 1.0;
	  
	  prev_j = cur_j;
	}
	
	//update prev_center
	switch (cept_start_mode_) {
	case IBM4CENTER:
	  prev_center = (uint) round(sum_j / nAlignedWords);
	  break;
	case IBM4FIRST:
	  prev_center = first_j;
	  break;
	case IBM4LAST:
	  prev_center = prev_j;
	  break;
	case IBM4UNIFORM:
	  prev_center = (uint) round(sum_j / nAlignedWords);
	  break;	  
	}
      }
    }
  }

  //now that all counts are collected, initialize the distributions
  
  //a) cept start
  for (uint x=0; x < cept_start_prob_.xDim(); x++) {
    for (uint y=0; y < cept_start_prob_.yDim(); y++) {

      double sum = 0.0;
      for (uint d=0; d < cept_start_prob_.zDim(); d++)
	sum += cept_start_prob_(x,y,d);

      const double count_factor = 0.9 / sum;
      const double uniform_share = 0.1 / cept_start_prob_.zDim();

      for (uint d=0; d < cept_start_prob_.zDim(); d++)
	cept_start_prob_(x,y,d) = count_factor * cept_start_prob_(x,y,d) + uniform_share;
    }
  }

  //b) within-cept
  for (uint x=0; x < within_cept_prob_.xDim(); x++) {
    
    double sum = 0.0;
    for (uint d=0; d < within_cept_prob_.yDim(); d++)
      sum += within_cept_prob_(x,d);

    const double count_factor = 0.9 / sum;
    const double uniform_share = 0.1 / ( within_cept_prob_.yDim()-1 );
    
    for (uint d=0; d < within_cept_prob_.yDim(); d++) {
      if (d == displacement_offset_) {
	//zero-displacements are impossible within cepts
	within_cept_prob_(x,d) = 0.0;
      }
      else 
	within_cept_prob_(x,d) = count_factor * within_cept_prob_(x,d) + uniform_share;
    }
  }

  //c) sentence start prob
  if (use_sentence_start_prob_) {
    sentence_start_prob_ *= 1.0 / sentence_start_prob_.sum();
  }
}

void IBM4Trainer::update_alignments_unconstrained() {

  Math2D::NamedMatrix<long double> expansion_prob(MAKENAME(expansion_prob));
  Math2D::NamedMatrix<long double> swap_prob(MAKENAME(swap_prob));

  for (uint s=0; s < source_sentence_.size(); s++) {

    const uint curI = target_sentence_[s].size();
    Math1D::NamedVector<uint> fertility(curI+1,0,MAKENAME(fertility));
    
    uint nIter=0;
    update_alignment_by_hillclimbing(s,nIter,fertility,expansion_prob,swap_prob);
  }
}


long double IBM4Trainer::alignment_prob(uint s, const Math1D::Vector<ushort>& alignment) const {


  long double prob = 1.0;

  const Storage1D<uint>& cur_source = source_sentence_[s];
  const Storage1D<uint>& cur_target = target_sentence_[s];
  const Math2D::Matrix<uint>& cur_lookup = slookup_[s];

  const uint curI = target_sentence_[s].size();
  const uint curJ = source_sentence_[s].size();

  assert(alignment.size() == curJ);

  Math1D::NamedVector<uint> fertility(curI+1,0,MAKENAME(fertility));
  Math1D::NamedVector<std::set<uint> > aligned_source_words(curI+1,MAKENAME(aligned_source_words));
  
  for (uint j=0; j < curJ; j++) {
    const uint aj = alignment[j];
    aligned_source_words[aj].insert(j);
    fertility[aj]++;
    
    if (aj == 0)
      prob *= dict_[0][cur_source[j]-1];      
  }
  
  if (curJ < 2*fertility[0])
    return 0.0;

  for (uint i=1; i <= curI; i++) {
    uint t_idx = cur_target[i-1];
    prob *= fertility_prob_[t_idx][fertility[i]];
    if (!no_factorial_)
      prob *= ldfac(fertility[i]);
  }

  //handle cepts with one or more aligned source words
  int prev_cept_center = -1;

  for (uint i=1; i <= curI; i++) {

    //NOTE: a dependence on word classes is currently not implemented
    
    if (fertility[i] > 0) {
      const uint ti = cur_target[i-1];

      const uint first_j = *aligned_source_words[i].begin();

      //handle the head of the cept
      if (prev_cept_center != -1) {

	const uint first_j = *aligned_source_words[i].begin();
	int displacement = first_j - prev_cept_center;
	prob *= dict_[ti][cur_lookup(first_j,i-1)];
	if (cept_start_mode_ != IBM4UNIFORM)
	  prob *= cept_start_prob_(0,0,displacement + displacement_offset_);
	else
	  prob /= curJ;
      }
      else {
	if (use_sentence_start_prob_) {
	  prob *= dict_[ti][cur_lookup(first_j,i-1)];
	  prob *= sentence_start_prob_[first_j];
	}
	else {
	  prob *= dict_[ti][cur_lookup(first_j,i-1)];
	  prob *= 1.0 / curJ;
	}
      }

      //handle the body of the cept
      uint prev_j = first_j;
      std::set<uint>::iterator ait = aligned_source_words[i].begin();
      for (++ait; ait != aligned_source_words[i].end(); ait++) {

	const uint cur_j = *ait;
	const int displacement = cur_j - prev_j;
	assert(abs(displacement) < (int) maxJ_);
	if (displacement + displacement_offset_ >= within_cept_prob_.size()) {

	  std::cerr << "J: " << curJ << std::endl;
	  std::cerr << "displacement = " << cur_j << " - " << prev_j << std::endl;
	  std::cerr << "offset: " << displacement_offset_ << std::endl;
	}
	prob *= dict_[ti][cur_lookup(cur_j,i-1)] * within_cept_prob_(0,displacement + displacement_offset_);
	
	prev_j = cur_j;
      }

      //compute the center of this cept and store the result in prev_cept_center
      double sum = 0.0;
      for (std::set<uint>::iterator ait = aligned_source_words[i].begin(); ait != aligned_source_words[i].end(); ait++) {
	sum += *ait;
      }

      switch (cept_start_mode_) {
      case IBM4CENTER :
	prev_cept_center = (uint) round(sum / fertility[i]);
	break;
      case IBM4FIRST:
	prev_cept_center = first_j;
	break;
      case IBM4LAST:
	prev_cept_center = prev_j;
	break;
      case IBM4UNIFORM:
	prev_cept_center = (uint) round(sum / fertility[i]);
	break;
      default:
	assert(false);
      }

      assert(prev_cept_center >= 0);
    }
  }

  //handle empty word
  assert(fertility[0] <= 2*curJ);

  //dictionary probs were handled above
  
  prob *= ldchoose(curJ-fertility[0],fertility[0]);
  for (uint k=1; k <= fertility[0]; k++)
    prob *= p_zero_;
  for (uint k=1; k <= curJ-2*fertility[0]; k++)
    prob *= p_nonzero_;

  if (och_ney_empty_word_) {

    for (uint k=1; k<= fertility[0]; k++)
      prob *= ((long double) k) / curJ;
  }

  return prob;
}


long double IBM4Trainer::update_alignment_by_hillclimbing(uint s, uint& nIter, Math1D::Vector<uint>& fertility,
							  Math2D::Matrix<long double>& expansion_prob,
							  Math2D::Matrix<long double>& swap_prob) {

  //std::cerr << "*************** hillclimb(" << s << ")" << std::endl;
  //   std::cerr << "start alignment: " << best_known_alignment_[s] << std::endl;

  const Storage1D<uint>& cur_target = target_sentence_[s];
  const Math2D::Matrix<uint>& cur_lookup = slookup_[s];
  
  const uint curI = target_sentence_[s].size();
  const uint curJ = source_sentence_[s].size();

  fertility.resize(curI+1);

  long double base_prob = alignment_prob(s,best_known_alignment_[s]);

  swap_prob.resize(curJ,curJ);
  expansion_prob.resize(curJ,curI+1);

  //DEBUG
  uint count_iter = 0;
  //END_DEBUG

  while (true) {    

    //source words are listed in ascending order
    Math1D::NamedVector< std::vector<uint> > aligned_source_words(curI+1,MAKENAME(aligned_source_words));

    fertility.set_constant(0);
    for (uint j=0; j < curJ; j++) {
      const uint aj = best_known_alignment_[s][j];
      fertility[aj]++;
      aligned_source_words[aj].push_back(j);
    }

    Math1D::NamedVector<uint> prev_cept(curI+1,MAX_UINT,MAKENAME(prev_cept));
    Math1D::NamedVector<uint> next_cept(curI+1,MAX_UINT,MAKENAME(next_cept));
    Math1D::NamedVector<uint> cept_center(curI+1,MAX_UINT,MAKENAME(cept_center));

    uint prev_i = MAX_UINT;
    for (uint i=1; i <= curI; i++) {

      if (fertility[i] > 0) {
	
	prev_cept[i] = prev_i;
	if (prev_i != MAX_UINT)
	  next_cept[prev_i] = i;

	switch (cept_start_mode_) {
	case IBM4CENTER: {
	  double sum_j = 0.0;
	  for (uint k=0; k < aligned_source_words[i].size(); k++)
	    sum_j += aligned_source_words[i][k];
	  cept_center = (uint) round(sum_j / aligned_source_words[i].size());
	  break;
	}
	case IBM4FIRST:
	  cept_center[i] = aligned_source_words[i][0];
	  break;
	case IBM4LAST:
	  cept_center[i] = aligned_source_words[i][aligned_source_words[i].size()-1];
	  break;
	case IBM4UNIFORM:
	  break;
	default:
	  assert(false);
	}

	prev_i = i;
      }
    }

    //DEBUG
    count_iter++;
    //END_DEBUG
    nIter++;
    //std::cerr << "****************** starting new hillclimb iteration, current best prob: " << base_prob << std::endl;

    bool improvement = false;

    double best_prob = base_prob;
    bool best_change_is_move = false;
    uint best_move_j = MAX_UINT;
    uint best_move_aj = MAX_UINT;
    uint best_swap_j1 = MAX_UINT;
    uint best_swap_j2 = MAX_UINT;

    /**** scan neighboring alignments and keep track of the best one that is better 
     ****  than the current alignment  ****/

    timeval tStartExp,tEndExp;
    gettimeofday(&tStartExp,0);

    //a) expansion moves

    for (uint j=0; j < curJ; j++) {

      const uint aj = best_known_alignment_[s][j];
      assert(fertility[aj] > 0);
      //const uint cur_aj_fert = fertility[aj];
      expansion_prob(j,aj) = 0.0;
      
      const uint prev_i = prev_cept[aj];
      const uint next_i = next_cept[aj];

      //std::cerr << "j: " << j << ", aj: " << aj << std::endl;

      for (uint cand_aj = 0; cand_aj <= curI; cand_aj++) {

	if (cand_aj != aj) {

	  long double hyp_prob = 0.0;

// 	  Math1D::Vector<uint> hyp_alignment = best_known_alignment_[s];
// 	  hyp_alignment[j] = cand_aj;
// 	  hyp_prob = alignment_prob(s,hyp_alignment);

	  bool incremental_calculation = false;

	  if (aj != 0 && cand_aj != 0) {

	    if (next_i != MAX_UINT &&
		(((prev_i != MAX_UINT && cand_aj < prev_i) || (prev_i == MAX_UINT && cand_aj > next_i)) 
		 || ((next_i != MAX_UINT && cand_aj > next_i) 
		     || (next_i == MAX_UINT && prev_i != MAX_UINT && cand_aj < prev_i)   )  ) ) {

	      incremental_calculation = true;

	      const uint prev_ti = cur_target[aj-1];
	      const uint new_ti = cur_target[cand_aj-1];

	      long double incoming_prob = dict_[new_ti][cur_lookup(j,cand_aj-1)];

	      incoming_prob *= fertility_prob_[new_ti][fertility[cand_aj]+1];
	      incoming_prob *= fertility_prob_[prev_ti][fertility[aj]-1];

	      if (!no_factorial_) {
		incoming_prob *= ldfac(fertility[cand_aj]+1);
		incoming_prob *= ldfac(fertility[aj]-1);
	      }

	      long double leaving_prob = dict_[prev_ti][cur_lookup(j,aj-1)];

	      leaving_prob *= fertility_prob_[new_ti][fertility[cand_aj]];
	      leaving_prob *= fertility_prob_[prev_ti][fertility[aj]];

	      if (!no_factorial_) {
		leaving_prob *= ldfac(fertility[cand_aj]);
		leaving_prob *= ldfac(fertility[aj]);
	      }

	      const uint prev_aj_fert = fertility[aj];

	      /***************************** 1. changes regarding aj ******************************/
	      if (prev_aj_fert > 1) {
		//the cept aj remains

		uint jnum;
		for (jnum = 0; jnum < prev_aj_fert; jnum++) {
		  if (aligned_source_words[aj][jnum] == j)
		    break;
		}

		//std::cerr << "jnum: " << jnum << std::endl;

		assert (jnum < aligned_source_words[aj].size());

		//calculate new center of aj
		uint new_aj_center = MAX_UINT;
		switch (cept_start_mode_) {
		case IBM4CENTER : {
		  double sum_j = 0.0;
		  for (uint k=0; k < prev_aj_fert; k++) {
		    if (k != jnum)
		      sum_j += aligned_source_words[aj][k];
		  }
		  new_aj_center = (uint) round(sum_j / (aligned_source_words[aj].size()-1));
		  break;
		}
		case IBM4FIRST : {
		  if (jnum == 0)
		    new_aj_center = aligned_source_words[aj][1];
		  else {
		    new_aj_center = aligned_source_words[aj][0];
		    assert(new_aj_center == cept_center[aj]);
		  }
		  break;
		}
		case IBM4LAST : {
		  if (jnum+1 == prev_aj_fert)
		    new_aj_center = aligned_source_words[aj][prev_aj_fert-2];
		  else {
		    new_aj_center = aligned_source_words[aj][prev_aj_fert-1];
		    assert(new_aj_center == cept_center[aj]);
		  }
		  break;
		}
		case IBM4UNIFORM :
		  break;
		default:
		  assert(false);
		}
		

// 		std::cerr << "old aj center: " << cept_center[aj] << std::endl;
// 		std::cerr << "new_aj_center: " << new_aj_center << std::endl;
// 		std::cerr << "prev_i: " << prev_i << ", next_i: " << next_i << std::endl;


		//re-calculate the transition aj -> next_i
		if (next_i != MAX_UINT) {
		  int old_diff = ((int) aligned_source_words[next_i][0]) - ((int) cept_center[aj]);
		  leaving_prob *= cept_start_prob_(0,0,old_diff + displacement_offset_);
		  int new_diff = ((int) aligned_source_words[next_i][0]) - ((int) new_aj_center);
		  incoming_prob *= cept_start_prob_(0,0,new_diff + displacement_offset_);
		}

		if (jnum == 0) {
		  //the transition prev_i -> aj is affected
		  
		  if (prev_i != MAX_UINT) {
		    int old_head_diff = ((int) j) - ((int) cept_center[prev_i] );
		    old_head_diff += displacement_offset_;
		    leaving_prob *= cept_start_prob_(0,0,old_head_diff);  
		  }
		  else if (use_sentence_start_prob_) {
		    leaving_prob *= sentence_start_prob_[j];
		  }

		  int old_within_diff = ((int) aligned_source_words[aj][1]) - ((int) j);
		  old_within_diff += displacement_offset_;
		  leaving_prob *= within_cept_prob_(0, old_within_diff);

		  if (prev_i != MAX_UINT) {
		    int new_head_diff = ((int) aligned_source_words[aj][1]) - ((int) cept_center[prev_i] );
		    incoming_prob *= cept_start_prob_(0,0,new_head_diff + displacement_offset_);  
		  }
		  else if (use_sentence_start_prob_)
		    incoming_prob *= sentence_start_prob_[aligned_source_words[aj][1]];
		}
		else {
		  
		  int diff = ((int) aligned_source_words[aj][jnum]) - ((int) aligned_source_words[aj][jnum-1]);
		  leaving_prob *= within_cept_prob_(0,diff+displacement_offset_);

		  if (jnum+1 < prev_aj_fert) {
		    int old_diff = ((int) aligned_source_words[aj][jnum+1]) - ((int) aligned_source_words[aj][jnum]);
		    leaving_prob *= within_cept_prob_(0,old_diff+displacement_offset_);

		    int new_diff = ((int) aligned_source_words[aj][jnum+1]) - ((int) aligned_source_words[aj][jnum-1]);
		    incoming_prob *= within_cept_prob_(0,new_diff+displacement_offset_);
		  }
		}

	      }
	      else {
		//the cept aj vanishes

		//erase the transitions prev_i -> aj    and    aj -> next_i
		if (prev_i != MAX_UINT) {
		  int ldiff;
		  ldiff = ((int) j) - ((int) cept_center[prev_i]);
		  leaving_prob *= cept_start_prob_(0,0,ldiff + displacement_offset_);
		}		
		else if (use_sentence_start_prob_)
		  leaving_prob *= sentence_start_prob_[j];

		if (next_i != MAX_UINT) {
		  int ldiff = ((int) aligned_source_words[next_i][0]) - ((int) j);
		  leaving_prob *= cept_start_prob_(0,0,ldiff + displacement_offset_);
		}

		//introduce the transition prev_i -> next_i
		if (prev_i != MAX_UINT) {
		  if (next_i != MAX_UINT) {
		    int idiff = ((int) aligned_source_words[next_i][0]) - ((int) cept_center[prev_i]);
		    incoming_prob *= cept_start_prob_(0,0,idiff+displacement_offset_);
		  }
		}
		else if (use_sentence_start_prob_)
		  incoming_prob *= sentence_start_prob_[aligned_source_words[next_i][0]];
	      }
	      
	      /********************** 2. changes regarding cand_aj **********************/
	      uint cand_prev_i = MAX_UINT;
	      for (uint k=cand_aj-1; k > 0; k--) {
		if (fertility[k] > 0) {
		  cand_prev_i = k;
		  break;
		}
	      }
	      uint cand_next_i = MAX_UINT;
	      for (uint k=cand_aj+1; k <= curI; k++) {
		if (fertility[k] > 0) {
		  cand_next_i = k;
		  break;
		}
	      }

// 	      std::cerr << "cand_prev_i: " << cand_prev_i << std::endl;
// 	      std::cerr << "cand_next_i: " << cand_next_i << std::endl;

	      if (fertility[cand_aj] > 0) {
		//the cept cand_aj was already there

		uint insert_pos = 0;
		for (; insert_pos < fertility[cand_aj] 
		       && aligned_source_words[cand_aj][insert_pos] < j; insert_pos++) {
		  //empty body
		}

		//std::cerr << "insert_pos: " << insert_pos << std::endl;

		if (insert_pos == 0) {

		  if (cand_prev_i == MAX_UINT) {

		    if (use_sentence_start_prob_) {
		      leaving_prob *= sentence_start_prob_[aligned_source_words[cand_aj][0]];
		      incoming_prob *= sentence_start_prob_[j];

		      int wdiff = ((int) aligned_source_words[cand_aj][0]) - ((int) j);
		      incoming_prob *= within_cept_prob_(0,wdiff+displacement_offset_);
		    }
		  }
		  else {
		    int old_diff = ((int) aligned_source_words[cand_aj][0]) - ((int) cept_center[cand_prev_i]);
		    leaving_prob *= cept_start_prob_(0,0,old_diff+displacement_offset_);
		    
		    int new_diff = ((int) j) - ((int) cept_center[cand_prev_i]);
		    incoming_prob *= cept_start_prob_(0,0,new_diff+displacement_offset_);
		    int wdiff = ((int) aligned_source_words[cand_aj][0]) - ((int) j);
		    incoming_prob *= within_cept_prob_(0,wdiff+displacement_offset_);
		  }
		}
		else if (insert_pos < fertility[cand_aj]) {
		  
		  int old_diff = ((int) aligned_source_words[cand_aj][insert_pos]) 
		    - ((int) aligned_source_words[cand_aj][insert_pos-1]);
		  leaving_prob *= within_cept_prob_(0,old_diff+displacement_offset_);

		  int diff1 = ((int) j) - ((int) aligned_source_words[cand_aj][insert_pos-1]);
		  incoming_prob *= within_cept_prob_(0,diff1+displacement_offset_);
		  int diff2 = ((int) aligned_source_words[cand_aj][insert_pos]) - ((int) j);
		  incoming_prob *= within_cept_prob_(0,diff2+displacement_offset_);
		}
		else {
		  //insert at the end
		  assert(insert_pos == fertility[cand_aj]);

		  int diff = ((int) j) - ((int) aligned_source_words[cand_aj][insert_pos-1]);
		  incoming_prob *= within_cept_prob_(0,diff+displacement_offset_);

		  //std::cerr << "including prob. " << within_cept_prob_(0,diff+displacement_offset_) << std::endl;
		}

		if (cand_next_i != MAX_UINT) {
		  //calculate new center of cand_aj

		  uint new_cand_aj_center = MAX_UINT;
		  switch (cept_start_mode_) {
		  case IBM4CENTER : {
		    double sum_j = j;
		    for (uint k=0; k < fertility[cand_aj]; k++)
		      sum_j += aligned_source_words[cand_aj][k];

		    new_cand_aj_center = (uint) round(sum_j / (fertility[cand_aj]+1) );
		    break;
		  }
		  case IBM4FIRST : {
		    if (insert_pos == 0)
		      new_cand_aj_center = j;
		    else 
		      new_cand_aj_center = cept_center[cand_aj];
		    break;
		  }
		  case IBM4LAST : {
		    if (insert_pos > fertility[cand_aj])
		      new_cand_aj_center = j;
		    else
		      new_cand_aj_center = cept_center[cand_aj];
		    break;
		  }
		  case IBM4UNIFORM:
		    break;
		  default:
		    assert(false);
		  } //end of switch-statement
		  
		  if (new_cand_aj_center != cept_center[cand_aj]) {
		    int ldiff = aligned_source_words[cand_next_i][0] - cept_center[cand_aj];
		    leaving_prob *= cept_start_prob_(0,0,ldiff+displacement_offset_);
		    
		    int idiff = aligned_source_words[cand_next_i][0] - new_cand_aj_center;
		    incoming_prob *= cept_start_prob_(0,0,idiff+displacement_offset_);
		  }
		}
	      }
	      else {
		//the cept cand_aj is newly created

		//erase the transition cand_prev_i -> cand_next_i (if existent)
		if (cand_prev_i != MAX_UINT && cand_next_i != MAX_UINT) {

		  int old_diff = ((int) aligned_source_words[cand_next_i][0]) - ((int) cept_center[cand_prev_i]);
		  leaving_prob *= cept_start_prob_(0,0,old_diff + displacement_offset_);
		}
		else if (cand_prev_i == MAX_UINT) {
		  
		  assert(cand_next_i != MAX_UINT);
		  if (use_sentence_start_prob_)
		    leaving_prob *= sentence_start_prob_[aligned_source_words[cand_next_i][0]];
		}
		else {
		  //nothing to do here
		}

		//introduce the transitions cand_prev_i -> cand_aj    and   cand_aj -> cand_next_i
		if (cand_prev_i != MAX_UINT) {
		  int diff = ((int) j) - ((int) cept_center[cand_prev_i]);
		  incoming_prob *= cept_start_prob_(0,0,diff+displacement_offset_);
		}
		else
		  incoming_prob *= sentence_start_prob_[j];

		if (cand_next_i != MAX_UINT) {
		  int diff = ((int) aligned_source_words[cand_next_i][0]) - ((int) j);
		  incoming_prob *= cept_start_prob_(0,0,diff+displacement_offset_);
		}
	      }

	      hyp_prob = base_prob * incoming_prob / leaving_prob;

#if 0
	      //DEBUG
	      Math1D::Vector<uint> hyp_alignment = best_known_alignment_[s];
	      hyp_alignment[j] = cand_aj;
	      long double check_prob = alignment_prob(s,hyp_alignment);

 	      if (check_prob > 0.0) {
		
		long double check_ratio = hyp_prob / check_prob;
		
		if (! (check_ratio > 0.99 && check_ratio < 1.01)) {

		  std::cerr << "****************************************************************" << std::endl;
		  std::cerr << "expansion: moving j=" << j << " to cand_aj=" << cand_aj << " from aj=" << aj
			    << std::endl;
		  std::cerr << "sentence pair #" << s <<  ", curJ: " << curJ << ", curI: " << curI << std::endl;
		  std::cerr << "base alignment: " << best_known_alignment_[s] << std::endl;
		  std::cerr << "actual prob: " << check_prob << std::endl;
		  std::cerr << "incremental hyp_prob: " << hyp_prob << std::endl;		  
		  std::cerr << "(base_prob: " << base_prob << ")" << std::endl;
		  std::cerr << "################################################################" << std::endl;
		}
		
		if (check_prob > 1e-15 * base_prob)
		  assert(check_ratio > 0.99 && check_ratio < 1.01);
	      }
	      //END_DEBUG
#endif
	      
	    }
	  }
	  else if (cand_aj == 0) {
	    //NOTE: this time we also handle the cases where next_i == MAX_UINT or where prev_i == MAX_UINT

	    incremental_calculation = true;

	    assert(aj != 0);

	    const uint prev_ti = cur_target[aj-1];
	    const uint prev_aj_fert = fertility[aj];
	    const uint prev_zero_fert = fertility[0];
	    const uint new_zero_fert = prev_zero_fert+1;

	    if (curJ < 2*new_zero_fert) {
	      hyp_prob = 0.0;
	    }
	    else {

	      long double incoming_prob = dict_[0][source_sentence_[s][j]-1];
	      incoming_prob *= fertility_prob_[prev_ti][fertility[aj]-1];
	      if (!no_factorial_)
		incoming_prob *= ldfac(fertility[aj]-1);

	      incoming_prob *= ldchoose(curJ-new_zero_fert,new_zero_fert);

	      for (uint k=1; k <= new_zero_fert; k++)
		incoming_prob *= p_zero_;
	      for (uint k=1; k <= curJ-2*new_zero_fert; k++)
		incoming_prob *= p_nonzero_;

	      if (och_ney_empty_word_) {
		for (uint k=1; k <= new_zero_fert; k++)
		  incoming_prob *= ((long double) k) / curJ;
	      }

	      long double leaving_prob = dict_[prev_ti][cur_lookup(j,aj-1)];
	      leaving_prob *= fertility_prob_[prev_ti][fertility[aj]];
	      if (!no_factorial_)
		leaving_prob *= ldfac(fertility[aj]);

	      leaving_prob *= ldchoose(curJ-prev_zero_fert,prev_zero_fert);

	      for (uint k=1; k <= prev_zero_fert; k++)
		leaving_prob *= p_zero_;
	      for (uint k=1; k <= curJ-2*prev_zero_fert; k++)
		leaving_prob *= p_nonzero_;

	      if (och_ney_empty_word_) {
		for (uint k=1; k <= prev_zero_fert; k++)
		  leaving_prob *= ((long double) k) / curJ;
	      }

	      if (prev_aj_fert > 1 ) {
		//the cept aj remains

		uint jnum;
		for (jnum = 0; jnum < prev_aj_fert; jnum++) {
		  if (aligned_source_words[aj][jnum] == j)
		    break;
		}

		//std::cerr << "jnum: " << jnum << std::endl;

		assert (jnum < aligned_source_words[aj].size());

		if (next_i != MAX_UINT) {
		  //calculate new center of aj
		  uint new_aj_center = MAX_UINT;
		  switch (cept_start_mode_) {
		  case IBM4CENTER : {
		    double sum_j = 0.0;
		    for (uint k=0; k < prev_aj_fert; k++) {
		      if (k != jnum)
			sum_j += aligned_source_words[aj][k];
		    }
		    new_aj_center = (uint) round(sum_j / (aligned_source_words[aj].size()-1));
		    break;
		  }
		  case IBM4FIRST : {
		    if (jnum == 0)
		      new_aj_center = aligned_source_words[aj][1];
		    else {
		      new_aj_center = aligned_source_words[aj][0];
		      assert(new_aj_center == cept_center[aj]);
		    }
		    break;
		  }
		  case IBM4LAST : {
		    if (jnum+1 == prev_aj_fert)
		      new_aj_center = aligned_source_words[aj][prev_aj_fert-2];
		    else {
		      new_aj_center = aligned_source_words[aj][prev_aj_fert-1];
		      assert(new_aj_center == cept_center[aj]);
		    }
		    break;
		  }
		  case IBM4UNIFORM :
		    break;
		  default:
		  assert(false);
		  }
		  
		  //  		std::cerr << "old aj center: " << cept_center[aj] << std::endl;
		  //  		std::cerr << "new_aj_center: " << new_aj_center << std::endl;
		  //  		std::cerr << "prev_i: " << prev_i << ", next_i: " << next_i << std::endl;

		  //re-calculate the transition aj -> next_i
		  int old_diff = ((int) aligned_source_words[next_i][0]) - ((int) cept_center[aj]);
		  leaving_prob *= cept_start_prob_(0,0,old_diff + displacement_offset_);
		  int new_diff = ((int) aligned_source_words[next_i][0]) - ((int) new_aj_center);
		  incoming_prob *= cept_start_prob_(0,0,new_diff + displacement_offset_);
		}

		if (jnum == 0) {
		  //the transition prev_i -> aj is affected

		  if (prev_i != MAX_UINT) {
		    int old_head_diff = ((int) j) - ((int) cept_center[prev_i] );
		    old_head_diff += displacement_offset_;
		    leaving_prob *= cept_start_prob_(0,0,old_head_diff);  
		  }
		  else if (use_sentence_start_prob_) {
		    leaving_prob *= sentence_start_prob_[j];
		  }
		  int old_within_diff = ((int) aligned_source_words[aj][1]) - ((int) j);
		  old_within_diff += displacement_offset_;
		  leaving_prob *= within_cept_prob_(0, old_within_diff);

		  if (prev_i != MAX_UINT) {
		    int new_head_diff = ((int) aligned_source_words[aj][1]) - ((int) cept_center[prev_i] );
		    incoming_prob *= cept_start_prob_(0,0,new_head_diff + displacement_offset_);  
		  }
		  else if (use_sentence_start_prob_)
		    incoming_prob *= sentence_start_prob_[aligned_source_words[aj][1]];
		}
		else {
		  
		  int diff = ((int) aligned_source_words[aj][jnum]) - ((int) aligned_source_words[aj][jnum-1]);
		  leaving_prob *= within_cept_prob_(0,diff+displacement_offset_);

		  if (jnum+1 < prev_aj_fert) {
		    int old_diff = ((int) aligned_source_words[aj][jnum+1]) - ((int) aligned_source_words[aj][jnum]);
		    leaving_prob *= within_cept_prob_(0,old_diff+displacement_offset_);

		    int new_diff = ((int) aligned_source_words[aj][jnum+1]) - ((int) aligned_source_words[aj][jnum-1]);
		    incoming_prob *= within_cept_prob_(0,new_diff+displacement_offset_);
		  }
		}
	      }
	      else {
		//the cept aj vanishes

		//erase the transitions prev_i -> aj    and    aj -> next_i
		if (prev_i != MAX_UINT) {
		  int ldiff;
		  ldiff = ((int) j) - ((int) cept_center[prev_i]);
		  leaving_prob *= cept_start_prob_(0,0,ldiff + displacement_offset_);
		}
		else if (use_sentence_start_prob_) {
		  leaving_prob *= sentence_start_prob_[j];
		}

		if (next_i != MAX_UINT) {
		  int ldiff = ((int) aligned_source_words[next_i][0]) - ((int) j);
		  leaving_prob *= cept_start_prob_(0,0,ldiff + displacement_offset_);
		  
		  //introduce the transition prev_i -> next_i
		  if (prev_i != MAX_UINT) {
		    int idiff = ((int) aligned_source_words[next_i][0]) - ((int) cept_center[prev_i]);
		    incoming_prob *= cept_start_prob_(0,0,idiff+displacement_offset_);		
		  }
		  else if (use_sentence_start_prob_) {
		    incoming_prob *= sentence_start_prob_[aligned_source_words[next_i][0]];
		  } 
		}
	      }
	    
	      hyp_prob = base_prob * incoming_prob / leaving_prob;

	      //DEBUG
// 	      Math1D::Vector<uint> hyp_alignment = best_known_alignment_[s];
// 	      hyp_alignment[j] = cand_aj;
// 	      long double check_prob = alignment_prob(s,hyp_alignment);
	      
// 	      if (check_prob != 0.0) {
		
// 		long double check_ratio = hyp_prob / check_prob;
		
// 		if (! (check_ratio > 0.99 && check_ratio < 1.01) ) {
// 		//if (true) {

// 		  std::cerr << "incremental prob: " << hyp_prob << std::endl;
// 		  std::cerr << "actual prob: " << check_prob << std::endl;

// 		  std::cerr << "pair #" << s << ", J=" << curJ << ", I=" << curI << std::endl;
// 		  std::cerr << "base alignment: " << best_known_alignment_[s] << std::endl;
// 		  std::cerr << "moving source word " << j << " from " << best_known_alignment_[s][j] << " to 0"
// 			    << std::endl;
// 		}
		
// 		if (check_prob > 1e-12 * best_prob)
// 		  assert (check_ratio > 0.99 && check_ratio < 1.01);
// 	      }
	      //END_DEBUG
	    }	      
	  }

	  if (!incremental_calculation) {
	    Math1D::Vector<ushort> hyp_alignment = best_known_alignment_[s];
	    hyp_alignment[j] = cand_aj;
	    hyp_prob = alignment_prob(s,hyp_alignment);
	  }
	    
	  expansion_prob(j,cand_aj) = hyp_prob;

	  assert(!isnan(expansion_prob(j,cand_aj)));
	  assert(!isinf(expansion_prob(j,cand_aj)));
	  
	  if (hyp_prob > 1.0000001*best_prob) {
	    //std::cerr << "improvement of " << (hyp_prob - best_prob) << std::endl;
	    
	    best_prob = hyp_prob;
	    improvement = true;
	    best_change_is_move = true;
	    best_move_j = j;
	    best_move_aj = cand_aj;
	  }
	}    
      }
    }

    gettimeofday(&tEndExp,0);
    //if (curJ >= 45 && curI >= 45)
    //std::cerr << "pair #" << s << ": spent " << diff_seconds(tEndExp,tStartExp) << " seconds on expansion moves" << std::endl;

    timeval tStartSwap,tEndSwap;
    gettimeofday(&tStartSwap,0);

    //b) swap moves
    for (uint j1=0; j1 < curJ; j1++) {

      swap_prob(j1,j1) = 0.0;
      //std::cerr << "j1: " << j1 << std::endl;
      
      const uint aj1 = best_known_alignment_[s][j1];

      for (uint j2 = j1+1; j2 < curJ; j2++) {

	//std::cerr << "j2: " << j2 << std::endl;

	const uint aj2 = best_known_alignment_[s][j2];

	if (aj1 == aj2) {
	  //we do not want to count the same alignment twice
	  swap_prob(j1,j2) = 0.0;
	}
	else {

	  long double hyp_prob = 0.0;

// 	  Math1D::Vector<uint> hyp_alignment = best_known_alignment_[s];
// 	  hyp_alignment[j1] = aj2;
// 	  hyp_alignment[j2] = aj1;
// 	  hyp_prob = alignment_prob(s,hyp_alignment);

	  if (aj1 != 0 && aj2 != 0 && 
	      cept_start_mode_ != IBM4UNIFORM &&
	      aligned_source_words[aj1].size() == 1 && aligned_source_words[aj2].size() == 1) {
	    //both affected cepts are one-word cepts

	    const uint taj1 = cur_target[aj1-1];
	    const uint taj2 = cur_target[aj2-1];

	    long double leaving_prob = 1.0;
	    long double incoming_prob = 1.0;

	    leaving_prob *= dict_[taj1][cur_lookup(j1,aj1-1)];
	    leaving_prob *= dict_[taj2][cur_lookup(j2,aj2-1)];
	    incoming_prob *= dict_[taj2][cur_lookup(j1,aj2-1)];
	    incoming_prob *= dict_[taj1][cur_lookup(j2,aj1-1)];

	    uint temp_aj1 = aj1;
	    uint temp_aj2 = aj2;
	    uint temp_j1 = j1;
	    uint temp_j2 = j2;
	    if (aj1 > aj2) {
	      temp_aj1 = aj2;
	      temp_aj2 = aj1;
	      temp_j1 = j2;
	      temp_j2 = j1;
	    }

	    // 1. entering cept temp_aj1
	    if (prev_cept[temp_aj1] != MAX_UINT) {
		
	      int prev_diff = temp_j1 - cept_center[prev_cept[temp_aj1]];
	      prev_diff += displacement_offset_;
	      int new_diff = prev_diff + temp_j2 - temp_j1;
	      
	      leaving_prob *= cept_start_prob_(0,0,prev_diff);
	      incoming_prob *= cept_start_prob_(0,0,new_diff);
	    }
	    else if (use_sentence_start_prob_) {
	      leaving_prob *= sentence_start_prob_[temp_j1];
	      incoming_prob *= sentence_start_prob_[temp_j2];
	    }

	    // 2. leaving cept temp_aj1 and entering cept temp_aj2
	    if (prev_cept[temp_aj2] != temp_aj1) {
	      
	      int prev_diff, new_diff;
	      //a) leaving cept aj1
	      uint next_i = next_cept[temp_aj1];
	      if (next_i != MAX_UINT) {
		
		prev_diff = aligned_source_words[next_i][0] - temp_j1;		  
		prev_diff += displacement_offset_;
		new_diff = prev_diff + temp_j1 - temp_j2;
		
		leaving_prob *= cept_start_prob_(0,0,prev_diff);
		incoming_prob *= cept_start_prob_(0,0,new_diff);
	      }
	      
	      //b) entering cept temp_aj2
	      prev_diff = temp_j2 - cept_center[prev_cept[temp_aj2]];
	      prev_diff += displacement_offset_;
	      new_diff = prev_diff + temp_j1 - temp_j2;
		
	      leaving_prob *= cept_start_prob_(0,0,prev_diff);
	      incoming_prob *= cept_start_prob_(0,0,new_diff);
	    }
	    else {
	      //leaving cept temp_aj1 is simultaneously entering cept temp_aj2
	      
	      //std::cerr << "--sim--" << std::endl;
		
	      int prev_diff = temp_j2 - temp_j1 + displacement_offset_;
	      int new_diff =  temp_j1 - temp_j2 + displacement_offset_;
	      
	      leaving_prob *= cept_start_prob_(0,0,prev_diff);
	      incoming_prob *= cept_start_prob_(0,0,new_diff);
	    }
	    
	    // 3. leaving cept temp_aj2
	    if (next_cept[temp_aj2] != MAX_UINT) {
	      
	      int prev_diff = aligned_source_words[next_cept[temp_aj2]][0] - temp_j2;
	      prev_diff += displacement_offset_;
	      int new_diff = prev_diff + temp_j2 - temp_j1;
	      
	      leaving_prob *= cept_start_prob_(0,0,prev_diff);
	      incoming_prob *= cept_start_prob_(0,0,new_diff);
	    }
	  
	    hyp_prob = base_prob * incoming_prob / leaving_prob;

	    //DEBUG
// 	    Math1D::Vector<uint> hyp_alignment = best_known_alignment_[s];
// 	    hyp_alignment[j1] = aj2;
// 	    hyp_alignment[j2] = aj1;
// 	    long double check_prob = alignment_prob(s,hyp_alignment);

// 	    if (check_prob > 0.0) {
	      
// 	      long double check_ratio = check_prob / hyp_prob;

// 	      if (! (check_ratio > 0.99 && check_ratio < 1.01)) {

// 		std::cerr << "******* swapping " << j1 << "->" << aj1 << " and " << j2 << "->" << aj2 << std::endl;
// 		std::cerr << "sentence pair #" << s <<  ", curJ: " << curJ << ", curI: " << curI << std::endl;
// 		std::cerr << "base alignment: " << best_known_alignment_[s] << std::endl;
// 		std::cerr << "actual prob: " << check_prob << std::endl;
// 		std::cerr << "incremental_hyp_prob: " << hyp_prob << std::endl;
	      
// 	      }

// 	      assert(check_ratio > 0.99 && check_ratio < 1.01);
// 	    }
	    //END_DEBUG

	  }
	  else if (aj1 != 0 && aj2 != 0 && 
		   prev_cept[aj1] != aj2 && prev_cept[aj2] != aj1) {

	    const uint taj1 = cur_target[aj1-1];
	    const uint taj2 = cur_target[aj2-1];

	    long double leaving_prob = 1.0;
	    long double incoming_prob = 1.0;

	    leaving_prob *= dict_[taj1][cur_lookup(j1,aj1-1)];
	    leaving_prob *= dict_[taj2][cur_lookup(j2,aj2-1)];
	    incoming_prob *= dict_[taj2][cur_lookup(j1,aj2-1)];
	    incoming_prob *= dict_[taj1][cur_lookup(j2,aj1-1)];

	    uint temp_aj1 = aj1;
	    uint temp_aj2 = aj2;
	    uint temp_j1 = j1;
	    uint temp_j2 = j2;
	    if (aj1 > aj2) {
	      temp_aj1 = aj2;
	      temp_aj2 = aj1;
	      temp_j1 = j2;
	      temp_j2 = j1;
	    }

	    //std::cerr << "A1" << std::endl;

	    uint old_j1_num = MAX_UINT;
	    for (uint k=0; k < fertility[temp_aj1]; k++) {
	      if (aligned_source_words[temp_aj1][k] == temp_j1) {
		old_j1_num = k;
		break;
	      }
	    }
	    assert(old_j1_num != MAX_UINT);

	    uint old_j2_num = MAX_UINT;
	    for (uint k=0; k < fertility[temp_aj2]; k++) {
	      if (aligned_source_words[temp_aj2][k] == temp_j2) {
		old_j2_num = k;
		break;
	      }
	    }
	    assert(old_j2_num != MAX_UINT);

	    std::vector<uint> new_temp_aj1_aligned_source_words = aligned_source_words[temp_aj1];
	    new_temp_aj1_aligned_source_words[old_j1_num] = temp_j2;
	    std::sort(new_temp_aj1_aligned_source_words.begin(),new_temp_aj1_aligned_source_words.end());

	    //std::cerr << "B1" << std::endl;

	    uint new_temp_aj1_center = MAX_UINT;
	    switch (cept_start_mode_) {
	    case IBM4CENTER : {
	      double sum_j = 0.0;
	      for (uint k=0; k < fertility[temp_aj1]; k++) {
		sum_j += new_temp_aj1_aligned_source_words[k];
	      }
	      new_temp_aj1_center = (uint) round(sum_j / fertility[temp_aj1]);
	      break;
	    }
	    case IBM4FIRST : {
	      new_temp_aj1_center = new_temp_aj1_aligned_source_words[0];
	      break;
	    }
	    case IBM4LAST : {
	      new_temp_aj1_center = new_temp_aj1_aligned_source_words[fertility[temp_aj1]-1];	      
	      break;
	    }
	    case IBM4UNIFORM : {
	      break;
	    }
	    default: assert(false);
	    }

	    //std::cerr << "C1" << std::endl;
	    
	    const int old_head1 = aligned_source_words[temp_aj1][0];
	    const int new_head1 = new_temp_aj1_aligned_source_words[0];

	    //std::cerr << "D1" << std::endl;

	    if (old_head1 != new_head1) {
	      if (prev_cept[temp_aj1] != MAX_UINT) {
		int ldiff = old_head1 - ((int) cept_center[prev_cept[temp_aj1]]);
		leaving_prob *= cept_start_prob_(0,0,ldiff+displacement_offset_);
		int idiff = new_head1 - ((int) cept_center[prev_cept[temp_aj1]]);
		incoming_prob *= cept_start_prob_(0,0,idiff+displacement_offset_);
	      }
	      else if (use_sentence_start_prob_) {
		leaving_prob *= sentence_start_prob_[old_head1];
		incoming_prob *= sentence_start_prob_[new_head1];
	      }
	    }

	    //std::cerr << "E1" << std::endl;

	    for (uint k=1; k < fertility[temp_aj1]; k++) {
	      int ldiff = ((int) aligned_source_words[temp_aj1][k]) - ((int) aligned_source_words[temp_aj1][k-1]);
	      leaving_prob *= within_cept_prob_(0,ldiff+displacement_offset_);
	      int idiff = ((int) new_temp_aj1_aligned_source_words[k]) - ((int) new_temp_aj1_aligned_source_words[k-1]);
	      incoming_prob *= within_cept_prob_(0,idiff+displacement_offset_);;
	    }

	    //std::cerr << "F1" << std::endl;

	    //transition to next cept
	    if (next_cept[temp_aj1] != MAX_UINT) {
	      const int next_head = aligned_source_words[next_cept[temp_aj1]][0];
	      
	      int ldiff = next_head - ((int) cept_center[temp_aj1]);
	      leaving_prob *= cept_start_prob_(0,0,ldiff+displacement_offset_);
	      int idiff = next_head - ((int) new_temp_aj1_center);
	      incoming_prob *= cept_start_prob_(0,0,idiff+displacement_offset_);
	    }

	    //std::cerr << "G1" << std::endl;

	    std::vector<uint> new_temp_aj2_aligned_source_words = aligned_source_words[temp_aj2];
	    new_temp_aj2_aligned_source_words[old_j2_num] = temp_j1;
	    std::sort(new_temp_aj2_aligned_source_words.begin(),new_temp_aj2_aligned_source_words.end());

	    //std::cerr << "H1" << std::endl;

	    uint new_temp_aj2_center = MAX_UINT;
	    switch (cept_start_mode_) {
	    case IBM4CENTER : {
	      double sum_j = 0.0;
	      for (uint k=0; k < fertility[temp_aj2]; k++) {
		sum_j += new_temp_aj2_aligned_source_words[k];
	      }
	      new_temp_aj2_center = (uint) round(sum_j / fertility[temp_aj2]);
	      break;
	    }
	    case IBM4FIRST : {
	      new_temp_aj2_center = new_temp_aj2_aligned_source_words[0];
	      break;
	    }
	    case IBM4LAST : {
	      new_temp_aj2_center = new_temp_aj2_aligned_source_words[fertility[temp_aj2]-1];
	      break;
	    }
	    case IBM4UNIFORM : {
	      break;
	    }
	    default: assert(false);
	    }

	    const int old_head2 = aligned_source_words[temp_aj2][0];
	    const int new_head2 = new_temp_aj2_aligned_source_words[0];

	    //std::cerr << "I1" << std::endl;

	    if (old_head2 != new_head2) {
	      if (prev_cept[temp_aj2] != MAX_UINT) {
		int ldiff = old_head2 - ((int) cept_center[prev_cept[temp_aj2]]);
		leaving_prob *= cept_start_prob_(0,0,ldiff+displacement_offset_);
		int idiff = new_head2 - ((int) cept_center[prev_cept[temp_aj2]]);
		incoming_prob *= cept_start_prob_(0,0,idiff+displacement_offset_);
	      }
	      else if (use_sentence_start_prob_) {
		leaving_prob *= sentence_start_prob_[old_head2];
		incoming_prob *= sentence_start_prob_[new_head2];
	      }
	    }

	    //std::cerr << "J1" << std::endl;
	    
	    for (uint k=1; k < fertility[temp_aj2]; k++) {
	      int ldiff = ((int) aligned_source_words[temp_aj2][k]) - ((int) aligned_source_words[temp_aj2][k-1]);
	      leaving_prob *= within_cept_prob_(0,ldiff+displacement_offset_);
	      int idiff = ((int) new_temp_aj2_aligned_source_words[k]) - ((int) new_temp_aj2_aligned_source_words[k-1]);
	      incoming_prob *= within_cept_prob_(0,idiff+displacement_offset_);;
	    }

	    //std::cerr << "K1" << std::endl;

	    //transition to next cept
	    if (next_cept[temp_aj2] != MAX_UINT) {
	      const int next_head = aligned_source_words[next_cept[temp_aj2]][0];
	      
	      int ldiff = next_head - ((int) cept_center[temp_aj2]);
	      leaving_prob *= cept_start_prob_(0,0,ldiff+displacement_offset_);
	      int idiff = next_head - ((int) new_temp_aj2_center);
	      incoming_prob *= cept_start_prob_(0,0,idiff+displacement_offset_);
	    }

	    //std::cerr << "M1" << std::endl;

	    hyp_prob = base_prob * incoming_prob / leaving_prob;

	    //DEBUG
// 	    Math1D::Vector<uint> hyp_alignment = best_known_alignment_[s];
// 	    hyp_alignment[j1] = aj2;
// 	    hyp_alignment[j2] = aj1;
// 	    long double check_prob = alignment_prob(s,hyp_alignment);

// 	    if (check_prob > 0.0) {
	      
// 	      long double check_ratio = check_prob / hyp_prob;

// 	      if (! (check_ratio > 0.99 && check_ratio < 1.01)) {

// 		std::cerr << "******* swapping " << j1 << "->" << aj1 << " and " << j2 << "->" << aj2 << std::endl;
// 		std::cerr << "sentence pair #" << s <<  ", curJ: " << curJ << ", curI: " << curI << std::endl;
// 		std::cerr << "base alignment: " << best_known_alignment_[s] << std::endl;
// 		std::cerr << "actual prob: " << check_prob << std::endl;
// 		std::cerr << "incremental_hyp_prob: " << hyp_prob << std::endl;
// 		std::cerr << "(base prob: " << base_prob << ")" << std::endl;
// 	      }

// 	      if (check_prob > 1e-12*base_prob) 
// 		assert(check_ratio > 0.99 && check_ratio < 1.01);
// 	    }
	    //END_DEBUG


	  }
	  else {

	    Math1D::Vector<ushort> hyp_alignment = best_known_alignment_[s];
	    hyp_alignment[j1] = aj2;
	    hyp_alignment[j2] = aj1;
	    hyp_prob = alignment_prob(s,hyp_alignment);
	  }

	  assert(!isnan(hyp_prob));

	  swap_prob(j1,j2) = hyp_prob;

	  if (hyp_prob > 1.0000001*best_prob) {
	    
	    improvement = true;
	    best_change_is_move = false;
	    best_prob = hyp_prob;
	    best_swap_j1 = j1;
	    best_swap_j2 = j2;
	  }
	}

	assert(!isnan(swap_prob(j1,j2)));
	assert(!isinf(swap_prob(j1,j2)));

	swap_prob(j2,j1) = swap_prob(j1,j2);

      }
    }

    gettimeofday(&tEndSwap,0);
    // if (curJ >= 45 && curI >= 45)
    //   std::cerr << "pair #" << s << ": spent " << diff_seconds(tEndSwap,tStartSwap) 
    // 		<< " seconds on swap moves" << std::endl;


    //update alignment if a better one was found
    if (!improvement)
      break;

    //update alignment
    if (best_change_is_move) {
      uint cur_aj = best_known_alignment_[s][best_move_j];
      assert(cur_aj != best_move_aj);

      //std::cerr << "moving source pos" << best_move_j << " from " << cur_aj << " to " << best_move_aj << std::endl;

      best_known_alignment_[s][best_move_j] = best_move_aj;
      fertility[cur_aj]--;
      fertility[best_move_aj]++;
      //zero_fert = fertility[0];
    }
    else {
      //std::cerr << "swapping: j1=" << best_swap_j1 << std::endl;
      //std::cerr << "swapping: j2=" << best_swap_j2 << std::endl;

      uint cur_aj1 = best_known_alignment_[s][best_swap_j1];
      uint cur_aj2 = best_known_alignment_[s][best_swap_j2];

      //std::cerr << "old aj1: " << cur_aj1 << std::endl;
      //std::cerr << "old aj2: " << cur_aj2 << std::endl;

      assert(cur_aj1 != cur_aj2);
      
      best_known_alignment_[s][best_swap_j1] = cur_aj2;
      best_known_alignment_[s][best_swap_j2] = cur_aj1;
    }

    //std::cerr << "probability improved from " << base_prob << " to " << best_prob << std::endl;
    base_prob = best_prob;    
  }  

  return base_prob;
}

void IBM4Trainer::train_unconstrained(uint nIter) {

  std::cerr << "starting IBM4 training without constraints" << std::endl;

  double max_perplexity = 0.0;

  IBM4CeptStartModel fceptstart_count(1,1,2*maxJ_-1,MAKENAME(fceptstart_count));
  IBM4WithinCeptModel fwithincept_count(1,2*maxJ_-1,MAKENAME(fwithincept_count));
  Math1D::NamedVector<double> fsentence_start_count(maxJ_,MAKENAME(fsentence_start_count));

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

  double hillclimbtime = 0.0;
  double countcollecttime = 0.0;

  for (uint iter=1; iter <= nIter; iter++) {

    uint sum_iter = 0;

    fzero_count = 0.0;
    fnonzero_count = 0.0;

    fceptstart_count.set_constant(0.0);
    fwithincept_count.set_constant(0.0);
    fsentence_start_count.set_constant(0.0);

    for (uint i=0; i < nTargetWords; i++) {
      fwcount[i].set_constant(0.0);
      ffert_count[i].set_constant(0.0);
    }

    max_perplexity = 0.0;

    for (uint s=0; s < source_sentence_.size(); s++) {

      if ((s% 10000) == 0)
	std::cerr << "sentence pair #" << s << std::endl;
      
      const Storage1D<uint>& cur_source = source_sentence_[s];
      const Storage1D<uint>& cur_target = target_sentence_[s];
      const Math2D::Matrix<uint>& cur_lookup = slookup_[s];
      
      const uint curI = cur_target.size();
      const uint curJ = cur_source.size();
      
      Math1D::NamedVector<uint> fertility(curI+1,0,MAKENAME(fertility));

      Math2D::NamedMatrix<long double> swap_move_prob(curJ,curJ,MAKENAME(swap_move_prob));
      Math2D::NamedMatrix<long double> expansion_move_prob(curJ,curI+1,MAKENAME(expansion_move_prob));

      timeval tHillclimbStart, tHillclimbEnd;
      gettimeofday(&tHillclimbStart,0);
      
      const long double best_prob = update_alignment_by_hillclimbing(s,sum_iter,fertility,
								     expansion_move_prob,swap_move_prob);
      max_perplexity -= std::log(std::max<double>(best_prob,1e-300));

      gettimeofday(&tHillclimbEnd,0);

      hillclimbtime += diff_seconds(tHillclimbEnd,tHillclimbStart);

      const long double expansion_prob = expansion_move_prob.sum();
      const long double swap_prob =  0.5 * swap_move_prob.sum();

      const long double sentence_prob = best_prob + expansion_prob +  swap_prob;

      if (sentence_prob < 1e-305) 
	continue;

      const long double inv_sentence_prob = 1.0 / sentence_prob;

      /**** update empty word counts *****/
	
      double cur_zero_weight = best_prob;
      for (uint j=0; j < curJ; j++) {
	if (best_known_alignment_[s][j] == 0) {
	  
	  for (uint jj=j+1; jj < curJ; jj++) {
	    if (best_known_alignment_[s][jj] != 0)
	      cur_zero_weight += swap_move_prob(j,jj);
	  }
	}
      }
      cur_zero_weight *= inv_sentence_prob;
      cur_zero_weight /= (curJ - fertility[0]);

      assert(!isnan(cur_zero_weight));
      assert(!isinf(cur_zero_weight));
      
      fzero_count += cur_zero_weight * (fertility[0]);
      fnonzero_count += cur_zero_weight * (curJ - 2*fertility[0]);

      if (curJ >= 2*(fertility[0]+1)) {
	long double inc_zero_weight = 0.0;
	for (uint j=0; j < curJ; j++)
	  inc_zero_weight += expansion_move_prob(j,0);
	
	inc_zero_weight *= inv_sentence_prob;
	inc_zero_weight /= (curJ - fertility[0]-1);
	fzero_count += inc_zero_weight * (fertility[0]+1);
	fnonzero_count += inc_zero_weight * (curJ -2*(fertility[0]+1));

	assert(!isnan(inc_zero_weight));
	assert(!isinf(inc_zero_weight));
      }

      if (fertility[0] > 1) {
	long double dec_zero_weight = 0.0;
	for (uint j=0; j < curJ; j++) {
	  if (best_known_alignment_[s][j] == 0) {
	    for (uint i=1; i <= curI; i++)
	      dec_zero_weight += expansion_move_prob(j,i);
	  }
	}
      
	dec_zero_weight *= inv_sentence_prob;
	dec_zero_weight /= (curJ - fertility[0]+1);

	fzero_count += dec_zero_weight * (fertility[0]-1);
	fnonzero_count += dec_zero_weight * (curJ -2*(fertility[0]-1));

	assert(!isnan(dec_zero_weight));
	assert(!isinf(dec_zero_weight));
      }

      /**** update fertility counts *****/
      for (uint i=1; i <= curI; i++) {

	const uint cur_fert = fertility[i];
	const uint t_idx = cur_target[i-1];

	long double addon = sentence_prob;
	for (uint j=0; j < curJ; j++) {
	  if (best_known_alignment_[s][j] == i) {
	    for (uint ii=0; ii <= curI; ii++)
	      addon -= expansion_move_prob(j,ii);
	  }
	  else
	    addon -= expansion_move_prob(j,i);
	}
	addon *= inv_sentence_prob;

	double daddon = (double) addon;
	if (!(daddon > 0.0)) {
	  std::cerr << "STRANGE: fractional weight " << daddon << " for sentence pair #" << s << " with "
		    << curJ << " source words and " << curI << " target words" << std::endl;
	  std::cerr << "best alignment prob: " << best_prob << std::endl;
	  std::cerr << "sentence prob: " << sentence_prob << std::endl;
	  std::cerr << "" << std::endl;
	}

	ffert_count[t_idx][cur_fert] += addon;

	//NOTE: swap moves do not change the fertilities
	if (cur_fert > 0) {
	  long double alt_addon = 0.0;
	  for (uint j=0; j < curJ; j++) {
	    if (best_known_alignment_[s][j] == i) {
	      for (uint ii=0; ii <= curI; ii++) {
		if (ii != i)
		  alt_addon += expansion_move_prob(j,ii);
	      }
	    }
	  }

	  ffert_count[t_idx][cur_fert-1] += inv_sentence_prob * alt_addon;
	}

	if (cur_fert+1 < fertility_prob_[t_idx].size()) {

	  long double alt_addon = 0.0;
	  for (uint j=0; j < curJ; j++) {
	    if (best_known_alignment_[s][j] != i) {
	      alt_addon += expansion_move_prob(j,i);
	    }
	  }

	  ffert_count[t_idx][cur_fert+1] += inv_sentence_prob * alt_addon;
	}
      }

      /**** update dictionary counts *****/
      for (uint j=0; j < curJ; j++) {

	const uint s_idx = cur_source[j];
	const uint cur_aj = best_known_alignment_[s][j];

	long double addon = sentence_prob;
	for (uint i=0; i <= curI; i++) 
	  addon -= expansion_move_prob(j,i);
	for (uint jj=0; jj < curJ; jj++)
	  addon -= swap_move_prob(j,jj);

	addon *= inv_sentence_prob;
	if (cur_aj != 0) {
	  fwcount[cur_target[cur_aj-1]][cur_lookup(j,cur_aj-1)] += addon;
	}
	else {
	  fwcount[0][s_idx-1] += addon;
	}

	for (uint i=0; i <= curI; i++) {

	  if (i != cur_aj) {

	    long double addon = expansion_move_prob(j,i);
	    for (uint jj=0; jj < curJ; jj++) {
	      if (best_known_alignment_[s][jj] == i)
		addon += swap_move_prob(j,jj);
	    }
	    addon *= inv_sentence_prob;

	    if (i!=0) {
	      fwcount[cur_target[i-1]][cur_lookup(j,i-1)] += addon;
	    }
	    else {
	      fwcount[0][s_idx-1] += addon;
	    }
	  }
	}
      }

      timeval tCountCollectStart, tCountCollectEnd;
      gettimeofday(&tCountCollectStart,0);

      /**** update distortion counts *****/
      NamedStorage1D<std::set<uint> > aligned_source_words(curI+1,MAKENAME(aligned_source_words));
      Math1D::NamedVector<uint> cept_center(curI+1,MAX_UINT,MAKENAME(cept_center));

      //denotes the largest preceding target position that produces source words
      Math1D::NamedVector<uint> prev_cept(curI+1,MAX_UINT,MAKENAME(prev_cept));
      Math1D::NamedVector<uint> first_aligned_source_word(curI+1,MAX_UINT,
							  MAKENAME(first_aligned_source_word));
      Math1D::NamedVector<uint> second_aligned_source_word(curI+1,MAX_UINT,
							   MAKENAME(second_aligned_source_word));

      for (uint j=0; j < curJ; j++) {
	const uint cur_aj = best_known_alignment_[s][j];
	aligned_source_words[cur_aj].insert(j);	
      }

      uint cur_prev_cept = MAX_UINT;
      for (uint i=0; i <= curI; i++) {

	assert(aligned_source_words[i].size() == fertility[i]);

	if (fertility[i] > 0) {
	  
	  std::set<uint>::iterator ait = aligned_source_words[i].begin();
	  first_aligned_source_word[i] = *ait;

	  uint prev_j = *ait;
	  if (fertility[i] > 1) {
	    ait++;
	    second_aligned_source_word[i] = *ait;
	    prev_j = *ait;
	  } 	    

	  double sum = 0.0;
	  for (std::set<uint>::iterator ait = aligned_source_words[i].begin(); ait != aligned_source_words[i].end(); ait++) {
	    sum += *ait;
	  }

	  switch (cept_start_mode_) {
	  case IBM4CENTER:
	    cept_center[i] = (uint) round(sum / fertility[i]);
	    break;
	  case IBM4FIRST:
	    cept_center[i] = first_aligned_source_word[i];
	    break;
	  case IBM4LAST:
	    cept_center[i] = prev_j;
	    break;
	  case IBM4UNIFORM:
	    cept_center[i] = (uint) round(sum / fertility[i]);
	    break;
	  default:
	    assert(false);
	  }

	  prev_cept[i] = cur_prev_cept;
	  cur_prev_cept = i;
	}
      }

      // 1. handle viterbi alignment
      for (uint i=1; i < curI; i++) {

	long double cur_prob = inv_sentence_prob * best_prob;
	
	if (fertility[i] > 0) {
	  
	  //a) update head prob
	  if (prev_cept[i] != MAX_UINT) {

	    int diff = first_aligned_source_word[i] - cept_center[prev_cept[i]];
	    diff += displacement_offset_;

	    fceptstart_count(0,0,diff) += cur_prob;
	  }
	  else if (use_sentence_start_prob_)
	    fsentence_start_count[first_aligned_source_word[i]] += cur_prob;

	  //b) update within-cept prob
	  uint prev_aligned_j = first_aligned_source_word[i];
	  std::set<uint>::iterator ait = aligned_source_words[i].begin();
	  ait++;
	  for (;ait != aligned_source_words[i].end(); ait++) {

	    const uint cur_j = *ait;
	    uint diff = cur_j - prev_aligned_j;
	    diff += displacement_offset_;
	    fwithincept_count(0,diff) += cur_prob;

	    prev_aligned_j = cur_j;
	  }
	}
      }

      // 2. handle expansion moves
      for (uint exp_j=0; exp_j < curJ; exp_j++) {

	const uint cur_aj = best_known_alignment_[s][exp_j];

	for (uint exp_i=0; exp_i <= curI; exp_i++) {

	  long double cur_prob = expansion_move_prob(exp_j,exp_i);

	  if (cur_prob > best_prob * 1e-11) {

	    cur_prob *= inv_sentence_prob;

	    NamedStorage1D<std::set<uint> > exp_aligned_source_words(MAKENAME(exp_aligned_source_words));
	    exp_aligned_source_words = aligned_source_words;
	    
	    exp_aligned_source_words[cur_aj].erase(exp_j);
	    exp_aligned_source_words[exp_i].insert(exp_j);
	    
	    uint prev_center = MAX_UINT;
	    
	    for (uint i=1; i <= curI; i++) {
	    
	      if (!exp_aligned_source_words[i].empty()) {

		double sum_j = 0;
		uint nAlignedWords = 0;

		std::set<uint>::iterator ait = exp_aligned_source_words[i].begin();
		const uint first_j = *ait;
		sum_j += *ait;
		nAlignedWords++;
		
		//collect counts for the head model
		if (prev_center != MAX_UINT) {
		  int diff =  first_j - prev_center;
		  diff += displacement_offset_;
		  fceptstart_count(0,0,diff) += cur_prob;
		}
		else if (use_sentence_start_prob_)
		  fsentence_start_count[first_j] += cur_prob;

		//collect counts for the within-cept model
		uint prev_j = first_j;
		for (ait++; ait != exp_aligned_source_words[i].end(); ait++) {

		  const uint cur_j = *ait;
		  sum_j += cur_j;
		  nAlignedWords++;

		  int diff = cur_j - prev_j;
		  diff += displacement_offset_;
		  fwithincept_count(0,diff) += cur_prob;

		  prev_j = cur_j;
		}

		//update prev_center
		switch (cept_start_mode_) {
		case IBM4CENTER:
		  prev_center = (uint) round(sum_j / nAlignedWords);
		  break;
		case IBM4FIRST:
		  prev_center = first_j;
		  break;
		case IBM4LAST:
		  prev_center = prev_j;
		  break;
		case IBM4UNIFORM:
		  prev_center = (uint) round(sum_j / nAlignedWords);
		  break;
		default:
		  assert(false);
		}
	      }

	    }
	  }	  
	}
      }
      
      //3. handle swap moves
      for (uint swap_j1 = 0; swap_j1 < curJ; swap_j1++) {

	const uint aj1 = best_known_alignment_[s][swap_j1];

	for (uint swap_j2 = 0; swap_j2 < curJ; swap_j2++) {
	  
	  long double cur_prob = swap_move_prob(swap_j1, swap_j2);

	  if (cur_prob > best_prob * 1e-11) {

	    cur_prob *= inv_sentence_prob;

	    const uint aj2 = best_known_alignment_[s][swap_j2];

	    NamedStorage1D<std::set<uint> > exp_aligned_source_words(MAKENAME(exp_aligned_source_words));
	    exp_aligned_source_words = aligned_source_words;
	    exp_aligned_source_words[aj1].erase(swap_j1);
	    exp_aligned_source_words[aj1].insert(swap_j2);
	    exp_aligned_source_words[aj2].erase(swap_j2);
	    exp_aligned_source_words[aj2].insert(swap_j1);

	    uint prev_center = MAX_UINT;
	    
	    for (uint i=1; i <= curI; i++) {
	    
	      if (!exp_aligned_source_words[i].empty()) {

		double sum_j = 0;
		uint nAlignedWords = 0;

		std::set<uint>::iterator ait = exp_aligned_source_words[i].begin();
		const uint first_j = *ait;
		sum_j += *ait;
		nAlignedWords++;
		
		//collect counts for the head model
		if (prev_center != MAX_UINT) {
		  int diff =  first_j - prev_center;
		  diff += displacement_offset_;
		  fceptstart_count(0,0,diff) += cur_prob;
		}
		else if (use_sentence_start_prob_)
		  fsentence_start_count[first_j] += cur_prob;
		
		//collect counts for the within-cept model
		uint prev_j = first_j;
		for (ait++; ait != exp_aligned_source_words[i].end(); ait++) {

		  const uint cur_j = *ait;
		  sum_j += cur_j;
		  nAlignedWords++;

		  int diff = cur_j - prev_j;
		  diff += displacement_offset_;
		  fwithincept_count(0,diff) += cur_prob;

		  prev_j = cur_j;
		}

		//update prev_center
		switch (cept_start_mode_) {
		case IBM4CENTER:
		  prev_center = (uint) round(sum_j / nAlignedWords);
		  break;
		case IBM4FIRST:
		  prev_center = first_j;
		  break;
		case IBM4LAST:
		  prev_center = prev_j;
		  break;
		case IBM4UNIFORM:
		  prev_center = (uint) round(sum_j / nAlignedWords);
		  break;
		default:
		  assert(false);
		}
	      }

	    }	    
	  }
	}
      }
      
      gettimeofday(&tCountCollectEnd,0);
      countcollecttime += diff_seconds(tCountCollectEnd,tCountCollectStart);
    }

    /***** update probability models from counts *******/

    //update p_zero_ and p_nonzero_
    double fsum = fzero_count + fnonzero_count;
    p_zero_ = fzero_count / fsum;
    p_nonzero_ = fnonzero_count / fsum;

    std::cerr << "new p_zero: " << p_zero_ << std::endl;

    assert(!isnan(p_zero_));
    assert(!isnan(p_nonzero_));

    //DEBUG
    uint nZeroAlignments = 0;
    uint nAlignments = 0;
    for (uint s=0; s < source_sentence_.size(); s++) {

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
    if (dict_weight_sum == 0.0) {
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
	  std::cerr << "WARNING: did not update dictionary entries because the sum was " << sum << std::endl;
	}
      }
    }
    else {


      for (uint i=0; i < nTargetWords; i++) {
	
	const double sum = fwcount[i].sum();
	const double prev_sum = dict_[i].sum();

	if (sum > 1e-305) {
	  const double inv_sum = 1.0 / sum;
	  assert(!isnan(inv_sum));
	  
	  for (uint k=0; k < fwcount[i].size(); k++) {
	    dict_[i][k] = fwcount[i][k] * prev_sum * inv_sum;
	  }
	}
      }

      double alpha = 100.0;
      if (iter > 2)
	alpha = 1.0;
      if (iter > 5)
	alpha = 0.1;

      dict_m_step(fwcount, prior_weight_, dict_, alpha, 45);
    }

    //update fertility probabilities
    for (uint i=1; i < nTargetWords; i++) {

      //std::cerr << "i: " << i << std::endl;

      const double sum = ffert_count[i].sum();

      if (sum > 1e-305) {

	if (fertility_prob_[i].size() > 0) {
	  assert(sum > 0.0);     
	  const double inv_sum = 1.0 / sum;
	  assert(!isnan(inv_sum));
	  
	  for (uint f=0; f < fertility_prob_[i].size(); f++)
	    fertility_prob_[i][f] = inv_sum * ffert_count[i][f];
	}
	else {
	  std::cerr << "WARNING: target word #" << i << " does not occur" << std::endl;
	}
      }
      else {
	std::cerr << "WARNING: did not update fertility count because sum was " << sum << std::endl;
      }
    }

    //update distortion probabilities

    //a) cept-start
    for (uint x=0; x < cept_start_prob_.xDim(); x++) {
      for (uint y=0; y < cept_start_prob_.yDim(); y++) {

	double sum = 0.0;
	for (uint d=0; d < cept_start_prob_.zDim(); d++) 
	  sum += fceptstart_count(x,y,d);
	
	if (sum > 1e-305) {
	  const double inv_sum = 1.0 / sum;
	  for (uint d=0; d < cept_start_prob_.zDim(); d++) 
	    cept_start_prob_(x,y,d) = inv_sum * fceptstart_count(x,y,d);
	}
	else {
	  std::cerr << "WARNING: did not update cept start model because sum was " << sum << std::endl;
	}
      }
    }

    //b) within-cept
    for (uint x=0; x < within_cept_prob_.xDim(); x++) {
      
      double sum = 0.0;
      for (uint d=0; d < within_cept_prob_.yDim(); d++)
	sum += fwithincept_count(x,d);

      if (sum > 1e-305) {

	const double inv_sum = 1.0 / sum;
	for (uint d=0; d < within_cept_prob_.yDim(); d++)
	  within_cept_prob_(x,d) = inv_sum * fwithincept_count(x,d);
      }
      else {
	std::cerr << "WARNING: did not update within cept model because sum was " << sum << std::endl;
      }
    }

    //c) sentence start prob
    if (use_sentence_start_prob_) {
      fsentence_start_count *= 1.0 / fsentence_start_count.sum();
      sentence_start_prob_ = fsentence_start_count;
    }

    max_perplexity /= source_sentence_.size();

    std::cerr << "IBM4 max-perplexity in between iterations #" << (iter-1) << " and " << iter << ": "
	      << max_perplexity << std::endl;
    if (possible_ref_alignments_.size() > 0) {
      
      std::cerr << "#### IBM4-AER in between iterations #" << (iter-1) << " and " << iter << ": " << AER() << std::endl;
      std::cerr << "#### IBM4-fmeasure in between iterations #" << (iter-1) << " and " << iter << ": " << f_measure() << std::endl;
      std::cerr << "#### IBM4-DAE/S in between iterations #" << (iter-1) << " and " << iter << ": " 
		<< DAE_S() << std::endl;
    }

    std::cerr << (((double) sum_iter) / source_sentence_.size()) << " average hillclimbing iterations per sentence pair" 
	      << std::endl;     
  }

  std::cerr << "spent " << hillclimbtime << " seconds on IBM4-hillclimbing" << std::endl;
  std::cerr << "spent " << countcollecttime << " seconds on IBM4-distortion count collection" << std::endl;

}

void IBM4Trainer::train_viterbi(uint nIter) {


  std::cerr << "starting IBM4 Viterbi-training without constraints" << std::endl;

  double max_perplexity = 0.0;

  IBM4CeptStartModel fceptstart_count(1,1,2*maxJ_-1,MAKENAME(fceptstart_count));
  IBM4WithinCeptModel fwithincept_count(1,2*maxJ_-1,MAKENAME(fwithincept_count));
  Math1D::NamedVector<double> fsentence_start_count(maxJ_,MAKENAME(fsentence_start_count));

  const uint nTargetWords = dict_.size();

  NamedStorage1D<Math1D::Vector<double> > fwcount(nTargetWords,MAKENAME(fwcount));
  NamedStorage1D<Math1D::Vector<double> > ffert_count(nTargetWords,MAKENAME(ffert_count));

  for (uint i=0; i < nTargetWords; i++) {
    fwcount[i].resize(dict_[i].size());
    ffert_count[i].resize_dirty(fertility_prob_[i].size());
  }

  long double fzero_count;
  long double fnonzero_count;

  for (uint iter=1; iter <= nIter; iter++) {

    uint sum_iter = 0;

    fzero_count = 0.0;
    fnonzero_count = 0.0;

    fceptstart_count.set_constant(0.0);
    fwithincept_count.set_constant(0.0);
    fsentence_start_count.set_constant(0.0);

    for (uint i=0; i < nTargetWords; i++) {
      fwcount[i].set_constant(0.0);
      ffert_count[i].set_constant(0.0);
    }

    max_perplexity = 0.0;

    for (uint s=0; s < source_sentence_.size(); s++) {

      if ((s% 10000) == 0)
	std::cerr << "sentence pair #" << s << std::endl;
      
      const Storage1D<uint>& cur_source = source_sentence_[s];
      const Storage1D<uint>& cur_target = target_sentence_[s];
      const Math2D::Matrix<uint>& cur_lookup = slookup_[s];
      
      const uint curI = cur_target.size();
      const uint curJ = cur_source.size();
      
      Math1D::NamedVector<uint> fertility(curI+1,0,MAKENAME(fertility));

      Math2D::NamedMatrix<long double> swap_move_prob(curJ,curJ,MAKENAME(swap_move_prob));
      Math2D::NamedMatrix<long double> expansion_move_prob(curJ,curI+1,MAKENAME(expansion_move_prob));

      timeval tHillclimbStart, tHillclimbEnd;
      gettimeofday(&tHillclimbStart,0);
      
      const long double best_prob = update_alignment_by_hillclimbing(s,sum_iter,fertility,
								     expansion_move_prob,swap_move_prob);
      max_perplexity -= std::log(best_prob);

      gettimeofday(&tHillclimbEnd,0);

      const long double sentence_prob = best_prob;

      if (sentence_prob < 1e-305)
	continue;

      const long double inv_sentence_prob = 1.0 / sentence_prob;

      /**** update empty word counts *****/

      double cur_zero_weight = best_prob;
      cur_zero_weight *= inv_sentence_prob;
      cur_zero_weight /= (curJ - fertility[0]);

      assert(!isnan(cur_zero_weight));
      
      fzero_count += cur_zero_weight * (fertility[0]);
      fnonzero_count += cur_zero_weight * (curJ - 2*fertility[0]);

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
      NamedStorage1D<std::set<uint> > aligned_source_words(curI+1,MAKENAME(aligned_source_words));
      Math1D::NamedVector<uint> cept_center(curI+1,MAX_UINT,MAKENAME(cept_center));

      //denotes the largest preceding target position that produces source words
      Math1D::NamedVector<uint> prev_cept(curI+1,MAX_UINT,MAKENAME(prev_cept));
      Math1D::NamedVector<uint> first_aligned_source_word(curI+1,MAX_UINT,
							  MAKENAME(first_aligned_source_word));
      Math1D::NamedVector<uint> second_aligned_source_word(curI+1,MAX_UINT,
							   MAKENAME(second_aligned_source_word));

      for (uint j=0; j < curJ; j++) {
	const uint cur_aj = best_known_alignment_[s][j];
	aligned_source_words[cur_aj].insert(j);	
      }

      uint cur_prev_cept = MAX_UINT;
      for (uint i=0; i <= curI; i++) {

	assert(aligned_source_words[i].size() == fertility[i]);

	if (fertility[i] > 0) {
	  
	  std::set<uint>::iterator ait = aligned_source_words[i].begin();
	  first_aligned_source_word[i] = *ait;

	  uint prev_j = *ait;
	  if (fertility[i] > 1) {
	    ait++;
	    second_aligned_source_word[i] = *ait;
	    prev_j = *ait;
	  } 	    

	  double sum = 0.0;
	  for (std::set<uint>::iterator ait = aligned_source_words[i].begin(); ait != aligned_source_words[i].end(); ait++) {
	    sum += *ait;
	  }

	  switch (cept_start_mode_) {
	  case IBM4CENTER:
	    cept_center[i] = (uint) round(sum / fertility[i]);
	    break;
	  case IBM4FIRST:
	    cept_center[i] = first_aligned_source_word[i];
	    break;
	  case IBM4LAST:
	    cept_center[i] = prev_j;
	    break;
	  case IBM4UNIFORM:
	    cept_center[i] = (uint) round(sum / fertility[i]);
	    break;
	  default:
	    assert(false);
	  }

	  prev_cept[i] = cur_prev_cept;
	  cur_prev_cept = i;
	}
      }

      // 1. handle viterbi alignment
      for (uint i=1; i < curI; i++) {

	if (fertility[i] > 0) {
	  
	  //a) update head prob
	  if (prev_cept[i] != MAX_UINT) {

	    int diff = first_aligned_source_word[i] - cept_center[prev_cept[i]];
	    diff += displacement_offset_;

	    fceptstart_count(0,0,diff) += 1.0;
	  }
	  else if (use_sentence_start_prob_)
	    fsentence_start_count[first_aligned_source_word[i]] += 1.0;

	  //b) update within-cept prob
	  uint prev_aligned_j = first_aligned_source_word[i];
	  std::set<uint>::iterator ait = aligned_source_words[i].begin();
	  ait++;
	  for (;ait != aligned_source_words[i].end(); ait++) {

	    const uint cur_j = *ait;
	    uint diff = cur_j - prev_aligned_j;
	    diff += displacement_offset_;
	    fwithincept_count(0,diff) += 1.0;

	    prev_aligned_j = cur_j;
	  }
	}
      }
    }


    /***** update probability models from counts *******/

    //update p_zero_ and p_nonzero_
    double fsum = fzero_count + fnonzero_count;
    p_zero_ = fzero_count / fsum;
    p_nonzero_ = fnonzero_count / fsum;

    std::cerr << "new p_zero: " << p_zero_ << std::endl;

    //DEBUG
    uint nZeroAlignments = 0;
    uint nAlignments = 0;
    for (uint s=0; s < source_sentence_.size(); s++) {

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

    //update fertility probabilities
    for (uint i=1; i < nTargetWords; i++) {

      //std::cerr << "i: " << i << std::endl;

      const double sum = ffert_count[i].sum();

      if (sum > 1e-305) {

	if (fertility_prob_[i].size() > 0) {
	  assert(sum > 0.0);     
	  const double inv_sum = 1.0 / sum;
	  assert(!isnan(inv_sum));
	  
	  for (uint f=0; f < fertility_prob_[i].size(); f++)
	    fertility_prob_[i][f] = inv_sum * ffert_count[i][f];
	}
	else {
	  std::cerr << "WARNING: target word #" << i << " does not occur" << std::endl;
	}
      }
      else {
	std::cerr << "WARNING: did not update fertility count because sum was " << sum << std::endl;
      }
    }

    //update distortion probabilities

    //a) cept-start
    for (uint x=0; x < cept_start_prob_.xDim(); x++) {
      for (uint y=0; y < cept_start_prob_.yDim(); y++) {

	double sum = 0.0;
	for (uint d=0; d < cept_start_prob_.zDim(); d++) 
	  sum += fceptstart_count(x,y,d);
	
	if (sum > 1e-305) {
	  const double inv_sum = 1.0 / sum;
	  for (uint d=0; d < cept_start_prob_.zDim(); d++) 
	    cept_start_prob_(x,y,d) = inv_sum * fceptstart_count(x,y,d);
	}
	else {
	  std::cerr << "WARNING: did not update cept start model because sum was " << sum << std::endl;
	}
      }
    }

    //b) within-cept
    for (uint x=0; x < within_cept_prob_.xDim(); x++) {
      
      double sum = 0.0;
      for (uint d=0; d < within_cept_prob_.yDim(); d++)
	sum += fwithincept_count(x,d);

      if (sum > 1e-305) {

	const double inv_sum = 1.0 / sum;
	for (uint d=0; d < within_cept_prob_.yDim(); d++)
	  within_cept_prob_(x,d) = inv_sum * fwithincept_count(x,d);
      }
      else {
	std::cerr << "WARNING: did not update within cept model because sum was " << sum << std::endl;
      }
    }

    //c) sentence start prob
    if (use_sentence_start_prob_) {
      fsentence_start_count *= 1.0 / fsentence_start_count.sum();
      sentence_start_prob_ = fsentence_start_count;
    }

    max_perplexity /= source_sentence_.size();

    std::cerr << "IBM4 max-perplexity in between iterations #" << (iter-1) << " and " << iter << ": "
	      << max_perplexity << std::endl;
    if (possible_ref_alignments_.size() > 0) {
      
      std::cerr << "#### IBM4-AER in between iterations #" << (iter-1) << " and " << iter << ": " << AER() << std::endl;
      std::cerr << "#### IBM4-fmeasure in between iterations #" << (iter-1) << " and " << iter << ": " << f_measure() << std::endl;
      std::cerr << "#### IBM4-DAE/S in between iterations #" << (iter-1) << " and " << iter << ": " 
		<< DAE_S() << std::endl;
    }

    std::cerr << (((double) sum_iter) / source_sentence_.size()) << " average hillclimbing iterations per sentence pair" 
	      << std::endl;     

  }


}