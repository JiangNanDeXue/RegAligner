/*** ported here from singleword_fertility_training ****/
/** author: Thomas Schoenemann. This file was generated while Thomas Schoenemann was with the University of Düsseldorf, Germany, 2012 ***/

#include "ibm4_training.hh"

#include "combinatoric.hh"
#include "timing.hh"
#include "projection.hh"
#include "training_common.hh" // for get_wordlookup(), dictionary and start-prob m-step 
#include "stl_util.hh"
#include "storage_stl_interface.hh"
#include "alignment_computation.hh"

#ifdef HAS_GZSTREAM
#include "gzstream.h"
#endif

#include <fstream>
#include <set>
#include "stl_out.hh"


IBM4CacheStruct::IBM4CacheStruct(uchar j, WordClassType sc, WordClassType tc) : j_(j), sclass_(sc), tclass_(tc) {}

bool operator<(const IBM4CacheStruct& c1, const IBM4CacheStruct& c2) {

  if (c1.j_ != c2.j_)
    return (c1.j_ < c2.j_);
  if (c1.sclass_ != c2.sclass_)
    return (c1.sclass_ < c2.sclass_);

  return c1.tclass_ < c2.tclass_;
}


IBM4Trainer::IBM4Trainer(const Storage1D<Storage1D<uint> >& source_sentence,
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
                         bool och_ney_empty_word,
                         bool use_sentence_start_prob,
                         bool no_factorial, 
                         bool reduce_deficiency,
                         bool nondeficient,  
                         IBM4CeptStartMode cept_start_mode, IBM4InterDistMode inter_dist_mode,
			 IBM4IntraDistMode intra_dist_mode, bool smoothed_l0, double l0_beta, double l0_fertpen)
: FertilityModelTrainer(source_sentence,slookup,target_sentence,dict,wcooc, nSourceWords,nTargetWords,
			prior_weight,och_ney_empty_word, smoothed_l0, l0_beta, l0_fertpen,no_factorial,
			sure_ref_alignments,possible_ref_alignments,log_table),
  cept_start_prob_(MAKENAME(cept_start_prob_)),
  within_cept_prob_(MAKENAME(within_cept_prob_)), 
  sentence_start_parameters_(MAKENAME(sentence_start_parameters_)),
  source_class_(source_class), target_class_(target_class),
  cept_start_mode_(cept_start_mode),
  inter_dist_mode_(inter_dist_mode), intra_dist_mode_(intra_dist_mode),
  use_sentence_start_prob_(use_sentence_start_prob), reduce_deficiency_(reduce_deficiency),
  nondeficient_(nondeficient), storage_limit_(12)
{

  const uint nDisplacements = 2*maxJ_-1;
  displacement_offset_ = maxJ_-1;

  inter_distortion_cache_.resize(maxJ_+1);

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

  nSourceClasses_ = max_source_class+1;
  nTargetClasses_ = max_target_class+1;

  cept_start_prob_.resize(nSourceClasses_,nTargetClasses_,2*maxJ_-1);
  if (intra_dist_mode_ == IBM4IntraDistModeTarget)
    within_cept_prob_.resize(nTargetClasses_,maxJ_);
  else
    within_cept_prob_.resize(nSourceClasses_,maxJ_);

  cept_start_prob_.set_constant(1.0 / nDisplacements);


  within_cept_prob_.set_constant(1.0 / (maxJ_-1));
  for (uint x=0; x < within_cept_prob_.xDim(); x++)
    within_cept_prob_(x,0) = 0.0;

  if (use_sentence_start_prob_) {
    sentence_start_parameters_.resize(maxJ_, 1.0 / maxJ_);
    sentence_start_prob_.resize(maxJ_+1);
  }

  std::set<uint> seenJs;
  for (uint s=0; s < source_sentence.size(); s++)
    seenJs.insert(source_sentence[s].size());

  inter_distortion_prob_.resize(maxJ_+1);
  intra_distortion_prob_.resize(maxJ_+1);

  if (use_sentence_start_prob_) {
    for (uint J=1; J <= maxJ_; J++) {
      if (seenJs.find(J) != seenJs.end()) {
	
	sentence_start_prob_[J].resize(J,0.0);
	
	for (uint j=0; j < J; j++)
	  sentence_start_prob_[J][j] = sentence_start_parameters_[j];
      }
    }
  }

  for (uint s=0; s < source_sentence_.size(); s++) {

    const uint curJ = source_sentence_[s].size();
    const uint curI = target_sentence_[s].size();

    uint max_t = 0;

    for (uint i=0; i < curI; i++) {
      const uint tclass = target_class_[target_sentence_[s][i]];
      max_t = std::max(max_t,tclass);
    }

    uint max_s = 0;

    for (uint j=0; j < curJ; j++) {
      const uint sclass = source_class_[source_sentence_[s][j]];
      max_s = std::max(max_s,sclass);
    }


    if (reduce_deficiency_ && !nondeficient_) {
      if (inter_distortion_prob_[curJ].xDim() < max_s+1 || inter_distortion_prob_[curJ].yDim() < max_t+1)
	inter_distortion_prob_[curJ].resize(std::max<uint>(inter_distortion_prob_[curJ].xDim(),max_s+1),
					    std::max<uint>(inter_distortion_prob_[curJ].yDim(),max_t+1));
    }

    if (intra_dist_mode_ == IBM4IntraDistModeTarget) {
      
      if (intra_distortion_prob_[curJ].xDim() < max_t+1)
        intra_distortion_prob_[curJ].resize_dirty(max_t+1,curJ,curJ);
    }
    else {

      if (intra_distortion_prob_[curJ].xDim() < max_s+1)
	intra_distortion_prob_[curJ].resize_dirty(max_s+1,curJ,curJ);
    }
  }



  for (uint J=1; J <= maxJ_; J++) {
    if (seenJs.find(J) != seenJs.end()) {

      for (int j1=0; j1 < (int) J; j1++) {
        for (int j2=j1+1; j2 < (int) J; j2++) {

          for (uint y = 0; y < intra_distortion_prob_[J].xDim(); y++) {
            intra_distortion_prob_[J](y,j2,j1) = within_cept_prob_(y,j2-j1);
          }
        }
      }

      if (reduce_deficiency_) {
	
	if (J <= storage_limit_ || nSourceClasses_ * nTargetClasses_ <= 10) {
	  
	  for (uint sclass = 0; sclass < inter_distortion_prob_[J].xDim(); sclass++) {
	    for (uint tclass = 0; tclass < inter_distortion_prob_[J].yDim(); tclass++) {
	  
	      if (inter_distortion_prob_[J](sclass,tclass).size() == 0) {
		inter_distortion_prob_[J](sclass,tclass).resize(J,J);
	      }
	    }
	  }
	}
	
      }
    }
  }

  //EXPERIMENTAL - expand SOME of the inter distortion matrices
  if (reduce_deficiency_ && !nondeficient_ && nSourceClasses_*nTargetClasses_ > 10) {

    Storage1D<Math2D::Matrix<uint> > combi_count(inter_distortion_prob_.size());

    for (uint J=1; J < combi_count.size(); J++) {

      combi_count[J].resize(nSourceClasses_,nTargetClasses_,0);
    }

    for (uint s=0; s < source_sentence_.size(); s++) {

      const uint curJ = source_sentence_[s].size();
      const uint curI = target_sentence_[s].size();
      
    
      for (uint i=0; i < curI; i++) {
        const uint tclass = target_class_[target_sentence_[s][i]];
        
        for (uint j=0; j < curJ; j++) {
          const uint sclass = source_class_[source_sentence_[s][j]];
          
          combi_count[curJ](sclass,tclass)++;
        }
      }
    }

    uint nExpanded = 0;

    for (uint J=storage_limit_+1; J < combi_count.size(); J++) {
      
      for (uint x=0; x < combi_count[J].xDim(); x++) {
        for (uint y=0; y < combi_count[J].yDim(); y++) { 
          
          if (combi_count[J](x,y) >= 1.2*J*J) {
            
            nExpanded++;
            
            if (inter_distortion_prob_[J].xDim() <= x || inter_distortion_prob_[J].yDim() <= y)
              inter_distortion_prob_[J].resize(nSourceClasses_,nTargetClasses_);
            
            inter_distortion_prob_[J](x,y).resize(J,J,0.0);
          }
        }
      }
    }
    
    std::cerr << "expanded " << nExpanded << " inter probs." << std::endl;
  }
  //END_EXPERIMENTAL

  if (reduce_deficiency_)
    par2nonpar_inter_distortion();
}

/*virtual*/ std::string IBM4Trainer::model_name() const {

  return "IBM-4";
}

const Math1D::Vector<double>& IBM4Trainer::sentence_start_parameters() const {

  return sentence_start_parameters_;
}

double IBM4Trainer::inter_distortion_prob(int j, int j_prev, uint sclass, uint tclass, uint J)  const {

  if (!reduce_deficiency_)
    return cept_start_prob_(sclass,tclass,j-j_prev+displacement_offset_);
  

  assert(inter_distortion_prob_[J].xDim() >= sclass && inter_distortion_prob_[J].yDim() >= tclass);

  if (inter_distortion_prob_[J].size() > 0 && inter_distortion_prob_[J](sclass,tclass).size() > 0) 
    return inter_distortion_prob_[J](sclass,tclass)(j,j_prev);

  IBM4CacheStruct cs(j,sclass,tclass);
  
  std::map<IBM4CacheStruct,float>::const_iterator it = inter_distortion_cache_[J][j_prev].find(cs);

  if (it == inter_distortion_cache_[J][j_prev].end()) {

    double sum = 0.0;
                
    for (int jj=0; jj < int(J); jj++) {
      sum += cept_start_prob_(sclass,tclass,jj-j_prev+displacement_offset_);
      assert(!isnan(sum));
    }
    
    float prob = std::max(1e-8,cept_start_prob_(sclass,tclass,j-j_prev+displacement_offset_) / sum);
    inter_distortion_cache_[J][j_prev][cs] = prob;
    return prob;
  }
  else
    return it->second;
}


void IBM4Trainer::par2nonpar_inter_distortion() {

  for (int J=1; J <= (int) maxJ_; J++) {

    if (inter_distortion_prob_[J].size() > 0) {

      for (uint x=0; x < inter_distortion_prob_[J].xDim(); x++) {
        for (uint y=0; y < inter_distortion_prob_[J].yDim(); y++) {

          if (inter_distortion_prob_[J](x,y).size() > 0) {

            assert(inter_distortion_prob_[J](x,y).xDim() == uint(J) && inter_distortion_prob_[J](x,y).yDim() == uint(J));

            if (reduce_deficiency_) {

              for (int j1=0; j1 < J; j1++) {
              
                double sum = 0.0;
                
                for (int j2=0; j2 < J; j2++) {
                  sum += cept_start_prob_(x,y,j2-j1+displacement_offset_);
                  assert(!isnan(sum));
                }
                
                if (sum > 1e-305) {
                  for (int j2=0; j2 < J; j2++) {
                    inter_distortion_prob_[J](x,y)(j2,j1) = 
                      std::max(1e-8,cept_start_prob_(x,y,j2-j1+displacement_offset_) / sum);
                  }
                }
                else if (j1 > 0) {
                  //std::cerr << "WARNING: sum too small for inter prob " << j1 << ", not updating." << std::endl;
                }
              }
            }
            else {
              for (int j1=0; j1 < J; j1++) 
                for (int j2=0; j2 < J; j2++) 
                  inter_distortion_prob_[J](x,y)(j2,j1) = cept_start_prob_(x,y,j2-j1+displacement_offset_);
            }
          }
        }
      }
    }
  }
}

void IBM4Trainer::par2nonpar_inter_distortion(int J, uint sclass, uint tclass) {

  if (inter_distortion_prob_[J].xDim() <= sclass || inter_distortion_prob_[J].xDim() <= tclass)
    inter_distortion_prob_[J].resize(std::max<uint>(inter_distortion_prob_[J].xDim(),sclass+1),
                                     std::max<uint>(inter_distortion_prob_[J].yDim(),tclass+1));
  if (inter_distortion_prob_[J](sclass,tclass).size() == 0)
    inter_distortion_prob_[J](sclass,tclass).resize(J,J,1.0 / J);

  if (reduce_deficiency_) {
  
    for (int j1=0; j1 < J; j1++) {
      
      double sum = 0.0;
      
      for (int j2=0; j2 < J; j2++) {
        sum += cept_start_prob_(sclass,tclass,j2-j1+displacement_offset_);
        assert(!isnan(sum));
      }
      
      if (sum > 1e-305) {
        for (int j2=0; j2 < J; j2++) {
          inter_distortion_prob_[J](sclass,tclass)(j2,j1) = 
            std::max(1e-8,cept_start_prob_(sclass,tclass,j2-j1+displacement_offset_) / sum);
        }
      }
      else if (j1 > 0) {
        //std::cerr << "WARNING: sum too small for inter prob " << j1 << ", not updating." << std::endl;
      }
    }
  }
  else {
    for (int j1=0; j1 < J; j1++) 
      for (int j2=0; j2 < J; j2++) 
        inter_distortion_prob_[J](sclass,tclass)(j2,j1) = cept_start_prob_(sclass,tclass,j2-j1+displacement_offset_);
  }
}


void IBM4Trainer::par2nonpar_intra_distortion() {

  for (int J=1; J <= (int) maxJ_; J++) {
    
    if (intra_distortion_prob_[J].size() > 0) {

      for (uint x=0; x < intra_distortion_prob_[J].xDim(); x++) {

        if (reduce_deficiency_) {

          for (int j1=0; j1 < J-1; j1++) {
            
            double sum = 0.0;
            
            for (int j2=j1+1; j2 < J; j2++) {
              sum += within_cept_prob_(x,j2-j1);
            }
            
            if (sum > 1e-305) {
              for (int j2=j1+1; j2 < J; j2++) {
                intra_distortion_prob_[J](x,j2,j1) = std::max(1e-8,within_cept_prob_(x,j2-j1) / sum);
              }
            }
            else {
              std::cerr << "WARNING: sum too small for intra prob " << j1 << ", J=" << J << ", not updating." << std::endl;
            }
          }
	}
        else {
          for (int j1=0; j1 < J-1; j1++)
            for (int j2=j1+1; j2 < J; j2++)
              intra_distortion_prob_[J](x,j2,j1) = within_cept_prob_(x,j2-j1);
        }
      }
    }
  }
}

double IBM4Trainer::inter_distortion_m_step_energy(const Storage1D<Storage2D<Math2D::Matrix<double> > >& inter_distort_count,
                                                   const std::map<DistortCount,double>& sparse_inter_distort_count,
                                                   const Math3D::Tensor<double>& inter_param, uint class1, uint class2) {

  double energy = 0.0;

  for (int J=1; J <= (int) maxJ_; J++) {

    if (inter_distort_count[J].xDim() > class1 && inter_distort_count[J].yDim() > class2) {

      const Math2D::Matrix<double>& cur_count = inter_distort_count[J](class1,class2);

      if (cur_count.size() == 0)
        continue;

      for (int j1=0; j1 < J; j1++) {

        double sum = 0.0;

        for (int j2=0; j2 < J; j2++) {
          sum += std::max(1e-15,inter_param(class1,class2,j2-j1 + displacement_offset_));
        }


        for (int j2=0; j2 < J; j2++) {

          const double count = cur_count(j2,j1);
          if (count == 0.0)
            continue;

          const double cur_param = std::max(1e-15, inter_param(class1,class2,j2-j1 + displacement_offset_));

          energy -= count * std::log( cur_param / sum);
          if (isnan(energy)) {

            std::cerr << "j1: " << j1 << ", j2: " << j2 << std::endl;
            std::cerr << "added " << cur_count(j2,j1) << "* log(" << cur_param 
                      << "/" << sum << ")" << std::endl;
          }
          assert(!isnan(energy));
        }
      }
    }
  }

  for (std::map<DistortCount,double>::const_iterator it = sparse_inter_distort_count.begin(); it != sparse_inter_distort_count.end(); it++) {

    const DistortCount& dist_count = it->first;
    const double weight = it->second;

    uchar J = dist_count.J_;
    int j1 = dist_count.j_prev_;

    double sum = 0.0;

    for (int j2=0; j2 < J; j2++) {
      sum += std::max(1e-15,inter_param(class1,class2,j2-j1 + displacement_offset_));
    }
    
    int j2 = dist_count.j_;

    const double cur_param = std::max(1e-15, inter_param(class1,class2,j2-j1 + displacement_offset_));

    energy -= weight * std::log( cur_param / sum);
    if (isnan(energy)) {
      
      std::cerr << "j1: " << j1 << ", j2: " << j2 << std::endl;
      std::cerr << "added " << weight << "* log(" << cur_param 
                << "/" << sum << ")" << std::endl;
    }
    assert(!isnan(energy));
  }

  return energy;
}

double IBM4Trainer::inter_distortion_m_step_energy(const Storage1D<Storage2D<Math2D::Matrix<double> > >& inter_distort_count,
                                                   const std::vector<std::pair<DistortCount,double> >& sparse_inter_distort_count,
                                                   const Math3D::Tensor<double>& inter_param, uint class1, uint class2) {


  double energy = 0.0;

  for (int J=1; J <= (int) maxJ_; J++) {

    if (inter_distort_count[J].xDim() > class1 && inter_distort_count[J].yDim() > class2) {

      const Math2D::Matrix<double>& cur_count = inter_distort_count[J](class1,class2);

      if (cur_count.size() == 0)
        continue;

      for (int j1=0; j1 < J; j1++) {

        double sum = 0.0;

        for (int j2=0; j2 < J; j2++) {
          sum += std::max(1e-15,inter_param(class1,class2,j2-j1 + displacement_offset_));
        }

        for (int j2=0; j2 < J; j2++) {

          const double count = cur_count(j2,j1);
          if (count == 0.0)
            continue;

          const double cur_param = std::max(1e-15, inter_param(class1,class2,j2-j1 + displacement_offset_));

          energy -= count * std::log( cur_param / sum);
          if (isnan(energy)) {

            std::cerr << "j1: " << j1 << ", j2: " << j2 << std::endl;
            std::cerr << "added " << cur_count(j2,j1) << "* log(" << cur_param 
                      << "/" << sum << ")" << std::endl;
          }
          assert(!isnan(energy));
        }
      }
    }
  }

  for (std::vector<std::pair<DistortCount,double> >::const_iterator it = sparse_inter_distort_count.begin(); 
       it != sparse_inter_distort_count.end(); it++) {

    const DistortCount& dist_count = it->first;
    const double weight = it->second;

    uchar J = dist_count.J_;
    int j1 = dist_count.j_prev_;

    double sum = 0.0;

    for (int j2=0; j2 < J; j2++) {
      sum += std::max(1e-15,inter_param(class1,class2,j2-j1 + displacement_offset_));
    }
    
    int j2 = dist_count.j_;

    const double cur_param = std::max(1e-15, inter_param(class1,class2,j2-j1 + displacement_offset_));

    energy -= weight * std::log( cur_param / sum);
    if (isnan(energy)) {
      
      std::cerr << "j1: " << j1 << ", j2: " << j2 << std::endl;
      std::cerr << "added " << weight << "* log(" << cur_param 
                << "/" << sum << ")" << std::endl;
    }
    assert(!isnan(energy));
  }

  return energy;
}

double IBM4Trainer::intra_distortion_m_step_energy(const Storage1D<Math3D::Tensor<double> >& intra_distort_count,
                                                   const Math2D::Matrix<double>& intra_param, uint word_class) {


  double energy = 0.0;

  for (int J=1; J <= (int) maxJ_; J++) {

    if (intra_distort_count[J].xDim() > word_class) {

      const Math3D::Tensor<double>& cur_count = intra_distort_count[J];

      for (int j1=0; j1 < J; j1++) {

        double sum = 0.0;

        for (int j2=j1+1; j2 < J; j2++) {
          sum += std::max(1e-15,intra_param(word_class,j2-j1));
        }

        for (int j2=j1+1; j2 < J; j2++) {
          double cur_param = std::max(1e-15, intra_param(word_class,j2-j1));

          energy -= cur_count(word_class,j2,j1) * std::log( cur_param / sum);
        }
      }
    }
  }
  
  return energy;
}

void IBM4Trainer::inter_distortion_m_step(const Storage1D<Storage2D<Math2D::Matrix<double> > >& inter_distort_count,
                                          const std::map<DistortCount,double>& sparse_inter_distort_count,
                                          uint class1, uint class2) {

  //iterating over a vector is a lot faster than iterating over a map -> copy
  std::vector<std::pair<DistortCount,double> > vec_sparse_inter_distort_count;
  vec_sparse_inter_distort_count.reserve(sparse_inter_distort_count.size());
  for (std::map<DistortCount,double>::const_iterator it = sparse_inter_distort_count.begin();
       it != sparse_inter_distort_count.end(); it++)
    vec_sparse_inter_distort_count.push_back(*it);

  Math3D::Tensor<double> new_ceptstart_prob = cept_start_prob_;
  Math3D::Tensor<double> hyp_ceptstart_prob = cept_start_prob_;
  Math1D::Vector<double> ceptstart_grad(cept_start_prob_.zDim());

  double alpha = 0.01;

  for (uint k=0; k < cept_start_prob_.zDim(); k++) 
    cept_start_prob_(class1,class2,k) = std::max(1e-15,cept_start_prob_(class1,class2,k));

  double energy = inter_distortion_m_step_energy(inter_distort_count,vec_sparse_inter_distort_count,cept_start_prob_,class1,class2);

  if (nSourceClasses_*nTargetClasses_ <= 4)
    std::cerr << "start energy: " << energy << std::endl;

  uint maxIter = (nSourceClasses_*nTargetClasses_ <= 4) ? 200 : 100;
  
  for (uint iter = 1; iter <= maxIter; iter++) {
    
    ceptstart_grad.set_constant(0.0);

    //compute gradient
    for (int J=1; J <= (int) maxJ_; J++) {

      if (inter_distort_count[J].xDim() > class1 && inter_distort_count[J].yDim() > class2) {

        const Math2D::Matrix<double>& cur_count = inter_distort_count[J](class1,class2);

        if (cur_count.size() == 0)
          continue;

        for (int j1=0; j1 < J; j1++) {

          double sum = 0.0;

          for (int j2=0; j2 < J; j2++) {
            sum += cept_start_prob_(class1,class2,j2-j1 + displacement_offset_);
            assert(!isnan(cept_start_prob_(class1,class2,j2-j1 + displacement_offset_)));
          }

          double count_sum = 0.0;
          for (int j2=0; j2 < J; j2++) {

            const double count = cur_count(j2,j1);

            if (count == 0.0)
              continue;

            count_sum += count;

            const double cur_param = cept_start_prob_(class1,class2,j2-j1 + displacement_offset_);
            ceptstart_grad[j2-j1 + displacement_offset_] -= 
              count / cur_param;
            assert(!isnan(ceptstart_grad[j2-j1 + displacement_offset_]));
          }

          for (int j2=0; j2 < J; j2++) {
            ceptstart_grad[j2-j1 + displacement_offset_] += count_sum / sum;
            assert(!isnan(ceptstart_grad[j2-j1 + displacement_offset_]));
          }
        }
      }
    }


    for (std::vector<std::pair<DistortCount,double> >::const_iterator it = vec_sparse_inter_distort_count.begin(); 
         it != vec_sparse_inter_distort_count.end(); it++) {

      const DistortCount& dist_count = it->first;
      const double weight = it->second;
      
      uchar J = dist_count.J_;
      int j1 = dist_count.j_prev_;
      
      double sum = 0.0;
      
      for (int j2=0; j2 < J; j2++) {
        sum += std::max(1e-15,cept_start_prob_(class1,class2,j2-j1 + displacement_offset_));
      }
      
      int j2 = dist_count.j_;
      
      const double cur_param = std::max(1e-15, cept_start_prob_(class1,class2,j2-j1 + displacement_offset_));

      ceptstart_grad[j2-j1 + displacement_offset_] -= weight / cur_param;

      assert(!isnan(ceptstart_grad[j2-j1 + displacement_offset_]));

      for (int jj=0; jj < J; jj++) {
        ceptstart_grad[jj-j1 + displacement_offset_] += weight / sum;
      }
    }

    //go in neg. gradient direction
    for (uint k=0; k < cept_start_prob_.zDim(); k++) 
      new_ceptstart_prob(class1,class2,k) = cept_start_prob_(class1,class2,k) - alpha * ceptstart_grad[k];


    //reproject
    Math1D::Vector<double> temp(cept_start_prob_.zDim());
    for (uint k=0; k < temp.size(); k++)
      temp[k] = new_ceptstart_prob(class1,class2,k);
    
    projection_on_simplex(temp.direct_access(),cept_start_prob_.zDim());

    for (uint k=0; k < temp.size(); k++)
      new_ceptstart_prob(class1,class2,k) = std::max(1e-15,temp[k]);
    

    double best_energy = 1e300;
    bool decreasing = true;

    double lambda = 1.0;
    double best_lambda = 1.0;

    uint nIter = 0;

    while (best_energy > energy || decreasing) {

      nIter++;

      lambda *= 0.5;
      double neg_lambda = 1.0 - lambda;

      for (uint k=0; k < cept_start_prob_.zDim(); k++) 
        hyp_ceptstart_prob(class1,class2,k) = neg_lambda * cept_start_prob_(class1,class2,k) 
          + lambda * new_ceptstart_prob(class1,class2,k);

      double hyp_energy = inter_distortion_m_step_energy(inter_distort_count,vec_sparse_inter_distort_count,hyp_ceptstart_prob,class1,class2);

      if (hyp_energy < best_energy) {

        decreasing = true;
        best_lambda = lambda;
        best_energy = hyp_energy;
      }
      else
        decreasing = false;

      if (nIter > 5 && best_energy < 0.975 * best_energy)
	break;

      if (nIter > 15 && lambda < 1e-12)
	break;
    }

    if (best_energy >= energy) {
      if (nSourceClasses_*nTargetClasses_ <= 4)
	std::cerr << "CUTOFF after " << iter << " iterations" << std::endl;
      break;
    }

    double neg_best_lambda = 1.0 - best_lambda;

    for (uint k=0; k < cept_start_prob_.zDim(); k++) 
      cept_start_prob_(class1,class2,k) = neg_best_lambda * cept_start_prob_(class1,class2,k) 
        + best_lambda * new_ceptstart_prob(class1,class2,k);

    energy = best_energy;

    if (  (nSourceClasses_*nTargetClasses_ <= 4) && (iter % 5) == 0)
      std::cerr << "iteration " << iter << ", inter energy: " << energy << std::endl;
  }
}

void IBM4Trainer::intra_distortion_m_step(const Storage1D<Math3D::Tensor<double> >& intra_distort_count,
                                          uint word_class) {


  Math2D::Matrix<double> new_within_cept_prob = within_cept_prob_;
  Math2D::Matrix<double> hyp_within_cept_prob = within_cept_prob_;
  Math1D::Vector<double> within_cept_grad(within_cept_prob_.yDim());

  double alpha = 0.01;

  for (uint k=0; k < within_cept_prob_.yDim(); k++) 
    within_cept_prob_(word_class,k) = std::max(1e-15,within_cept_prob_(word_class,k));

  double energy = intra_distortion_m_step_energy(intra_distort_count,within_cept_prob_,word_class);

  if (nTargetClasses_ <= 4)
    std::cerr << "start energy: " << energy << std::endl;

  const uint maxIter = 100;

  for (uint iter = 1; iter <= maxIter; iter++) {
    
    within_cept_grad.set_constant(0.0);

    //calculate gradient

    for (int J=1; J <= (int) maxJ_; J++) {

      const Math3D::Tensor<double>& cur_distort_count = intra_distort_count[J];

      if (cur_distort_count.xDim() > word_class) {

        for (int j1=0; j1 < J; j1++) {

          double sum = 0.0;

          for (int j2=j1+1; j2 < J; j2++) {
            sum += within_cept_prob_(word_class,j2-j1);
          }

          if (sum < 1e-100)
            continue;  //this can happen for j1=0 (and J=1)

          double count_sum = 0.0;
          for (int j2=j1+1; j2 < J; j2++) {

            const double cur_count = cur_distort_count(word_class,j2,j1);

            count_sum += cur_count;

            const double cur_param = within_cept_prob_(word_class,j2-j1);
            within_cept_grad[j2-j1] -= cur_count / cur_param;
          }

          for (int j2=j1+1; j2 < J; j2++) {
            within_cept_grad[j2-j1] += count_sum / sum;
          }
        }
      }
    }

    //go in neg. gradient direction
    for (uint k=0; k < within_cept_prob_.yDim(); k++) {

      new_within_cept_prob(word_class,k) = within_cept_prob_(word_class,k) - alpha * within_cept_grad[k];

      if (fabs(new_within_cept_prob(word_class,k)) > 1e75)
	  std::cerr << "error: abnormally large number: " << new_within_cept_prob(word_class,k) << std::endl;
    }

    //reproject
    Math1D::Vector<double> temp(within_cept_prob_.yDim());
    for (uint k=1; k < temp.size(); k++) 
      temp[k] = new_within_cept_prob(word_class,k);
    
    projection_on_simplex(temp.direct_access()+1,temp.size()-1); //the entry for 0 is always 0!

    for (uint k=1; k < temp.size(); k++)
      new_within_cept_prob(word_class,k) = std::max(1e-15,temp[k]);
    

    double best_energy = 1e300;
    bool decreasing = true;

    double lambda = 1.0;
    double best_lambda = 1.0;

    uint nIter = 0;

    while (best_energy > energy || decreasing) {

      nIter++;

      lambda *= 0.5;
      double neg_lambda = 1.0 - lambda;

      for (uint k=0; k < within_cept_prob_.yDim(); k++)
        hyp_within_cept_prob(word_class,k) = neg_lambda * within_cept_prob_(word_class,k) 
          + lambda * new_within_cept_prob(word_class,k);

      double hyp_energy = intra_distortion_m_step_energy(intra_distort_count,hyp_within_cept_prob,word_class);

      if (hyp_energy < best_energy) {

        decreasing = true;
        best_lambda = lambda;
        best_energy = hyp_energy;
      }
      else
        decreasing = false;

      if (nIter > 5 && best_energy < 0.975 * energy)
	break;

      if (nIter > 15 && lambda < 1e-12)
	break;
    }

    if (best_energy >= energy) {
      if (nTargetClasses_ <= 4)
	std::cerr << "CUTOFF after " << iter << " iterations" << std::endl;
      break;
    }

    double neg_best_lambda = 1.0 - best_lambda;

    for (uint k=0; k < within_cept_prob_.yDim(); k++) 
      within_cept_prob_(word_class,k) = neg_best_lambda * within_cept_prob_(word_class,k) 
        + best_lambda * new_within_cept_prob(word_class,k);

    energy = best_energy;

    if ((nTargetClasses_ <= 4) && (iter % 5) == 0)
    //if (word_class == 12)
      std::cerr << "iteration " << iter << ", intra energy: " << energy << std::endl;
  }
}

void IBM4Trainer::init_from_ibm3(FertilityModelTrainer& fert_trainer, bool clear_ibm3, 
				 bool collect_counts, bool viterbi) {

  std::cerr << "******** initializing IBM-4 from IBM-3 *******" << std::endl;

  fertility_prob_.resize(fert_trainer.fertility_prob().size());
  for (uint k=1; k < fertility_prob_.size(); k++) {
    fertility_prob_[k] = fert_trainer.fertility_prob()[k];

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


  for (size_t s=0; s < source_sentence_.size(); s++) 
    best_known_alignment_[s] = fert_trainer.best_alignments()[s];

  if (!fix_p0_) {
    p_zero_ = fert_trainer.p_zero();
    p_nonzero_ = 1.0 - p_zero_;
  }  

  if (collect_counts) {

    if (viterbi) {
      train_viterbi(1,&fert_trainer);
    }
    else {
      train_unconstrained(1,&fert_trainer);
    }

    iter_offs_ = 1;

    if (clear_ibm3)
      fert_trainer.release_memory();
  }
  else {

    if (clear_ibm3)
      fert_trainer.release_memory();
    
    //init distortion models from best known alignments
    cept_start_prob_.set_constant(0.0);
    within_cept_prob_.set_constant(0.0);
    sentence_start_parameters_.set_constant(0.0);
 
    for (size_t s=0; s < source_sentence_.size(); s++) {

      const Storage1D<uint>& cur_source = source_sentence_[s];
      const Storage1D<uint>& cur_target = target_sentence_[s];

      const uint curI = cur_target.size();
      const uint curJ = cur_source.size();
      
      NamedStorage1D<std::vector<int> > aligned_source_words(curI+1,MAKENAME(aligned_source_words));
    
      for (uint j=0; j < curJ; j++) {
	const uint aj = best_known_alignment_[s][j];
        aligned_source_words[aj].push_back(j);
      }
      
      int prev_center = -100;
      int prev_cept = -1;
    
      for (uint i=1; i <= curI; i++) {

	if (!aligned_source_words[i].empty()) {
	  
          const int first_j = aligned_source_words[i][0];

	  //collect counts for the head model
	  if (prev_center >= 0) {
	    const uint sclass = source_class_[cur_source[first_j]];
	    const uint tclass = (inter_dist_mode_ == IBM4InterDistModePrevious) ? 
	      target_class_[cur_target[prev_cept-1]] : target_class_[cur_target[i-1]];
	    
	    int diff =  first_j - prev_center;
	    diff += displacement_offset_;
	    cept_start_prob_(sclass,tclass,diff) += 1.0;
	  }
	  else if (use_sentence_start_prob_)
	    sentence_start_parameters_[first_j] += 1.0;
	  
	  //collect counts for the within-cept model
	  int prev_j = first_j;

          for (uint k=1; k < aligned_source_words[i].size(); k++) {

            const int cur_j = aligned_source_words[i][k];
	    
	    const uint tclass = target_class_[cur_target[i-1]];
	    const uint sclass = source_class_[cur_source[cur_j]];

	    int diff = cur_j - prev_j;
	    if (intra_dist_mode_ == IBM4IntraDistModeTarget)
	      within_cept_prob_(tclass,diff) += 1.0;
	    else
	      within_cept_prob_(sclass,diff) += 1.0;
	    
	    prev_j = cur_j;
	  }
	  
	  //update prev_center
	  switch (cept_start_mode_) {
	  case IBM4CENTER: {
            double sum_j = 0.0;
            for (uint k=0; k < aligned_source_words[i].size(); k++) 
              sum_j += aligned_source_words[i][k];
            prev_center = (int) round(sum_j / aligned_source_words[i].size());
	    break;
          }
	  case IBM4FIRST:
	    prev_center = first_j;
	    break;
	  case IBM4LAST: {
            prev_center = prev_j; //was set to the last pos in the above loop
          }
	    break;
	  case IBM4UNIFORM:
	    prev_center = first_j; //will not be used
	    break;	  
	  }
	  prev_cept = i;
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
	
        if (sum > 1e-300) {
          const double count_factor = 0.9 / sum;
          const double uniform_share = 0.1 / cept_start_prob_.zDim();
	
          for (uint d=0; d < cept_start_prob_.zDim(); d++)
            cept_start_prob_(x,y,d) = count_factor * cept_start_prob_(x,y,d) + uniform_share;
        }
        else {
          //this combination did not occur in the viterbi alignments
          //but it may still be possible in the data
          for (uint d=0; d < cept_start_prob_.zDim(); d++)
            cept_start_prob_(x,y,d) = 1.0 / cept_start_prob_.zDim();
        }
      }
    }

    par2nonpar_inter_distortion();

    //b) within-cept
    for (uint x=0; x < within_cept_prob_.xDim(); x++) {
    
      double sum = 0.0;
      for (uint d=0; d < within_cept_prob_.yDim(); d++)
	sum += within_cept_prob_(x,d);
     
      if (sum > 1e-300) {
        const double count_factor = 0.9 / sum;
        const double uniform_share = 0.1 / ( within_cept_prob_.yDim()-1 );
        
        for (uint d=0; d < within_cept_prob_.yDim(); d++) {
          if (d == 0) {
            //zero-displacements are impossible within cepts
            within_cept_prob_(x,d) = 0.0;
          }
          else 
            within_cept_prob_(x,d) = count_factor * within_cept_prob_(x,d) + uniform_share;
        }
      }
      else {
        for (uint d=0; d < within_cept_prob_.yDim(); d++) {
          if (d == 0) {
            //zero-displacements are impossible within cepts
            within_cept_prob_(x,d) = 0.0;
          }
          else 
            within_cept_prob_(x,d) = 1.0 / (within_cept_prob_.yDim()-1);
        }
      }
    }

    par2nonpar_intra_distortion();

    //c) sentence start prob
    if (use_sentence_start_prob_) {

      double sum = sentence_start_parameters_.sum();
      for (uint k=0; k < sentence_start_parameters_.size(); k++)
	sentence_start_parameters_[k] *= std::max(1e-15,sentence_start_parameters_[k] / sum);

      par2nonpar_start_prob(sentence_start_parameters_,sentence_start_prob_);
      //par2nonpar_start_prob();
    }
  }

  //DEBUG
#ifndef NDEBUG
  std::cerr << "checking" << std::endl;

  for (size_t s=0; s < source_sentence_.size(); s++) {

    long double align_prob = alignment_prob(s,best_known_alignment_[s]);

    if (isinf(align_prob) || isnan(align_prob) || align_prob == 0.0) {

      std::cerr << "ERROR: initial align-prob for sentence " << s << " has prob " << align_prob << std::endl;
      exit(1);
    }
  }
#endif
  //END_DEBUG
}

long double IBM4Trainer::alignment_prob(uint s, const Math1D::Vector<AlignBaseType>& alignment) {

  SingleLookupTable aux_lookup;
  
  const SingleLookupTable& lookup = get_wordlookup(source_sentence_[s],target_sentence_[s],wcooc_,
                                                   nSourceWords_,slookup_[s],aux_lookup);

  return alignment_prob(source_sentence_[s],target_sentence_[s],lookup,alignment);
}

long double IBM4Trainer::alignment_prob(const Storage1D<uint>& source, const Storage1D<uint>& target,
                                        const SingleLookupTable& lookup,const Math1D::Vector<AlignBaseType>& alignment) {

  long double prob = 1.0;

  const uint curI = target.size();
  const uint curJ = source.size();

  assert(alignment.size() == curJ);

  Math1D::NamedVector<uint> fertility(curI+1,0,MAKENAME(fertility));
  NamedStorage1D<std::vector<int> > aligned_source_words(curI+1,MAKENAME(aligned_source_words));

  const Math3D::Tensor<float>& cur_intra_distortion_prob =  intra_distortion_prob_[curJ];

  const Math1D::Vector<double>& cur_sentence_start_prob = sentence_start_prob_[curJ];
  
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
  }
  
  if (curJ < 2*fertility[0])
    return 0.0;

  for (uint i=1; i <= curI; i++) {
    uint t_idx = target[i-1];
    prob *= fertility_prob_[t_idx][fertility[i]];
    if (!no_factorial_)
      prob *= ld_fac_[fertility[i]]; 
  }

  //DEBUG
  if (isnan(prob))
    std::cerr << "prob nan after fertility probs" << std::endl;
  //END_DEBUG


  //handle cepts with one or more aligned source words
  int prev_cept_center = -1;
  int prev_cept = -1;

  for (uint i=1; i <= curI; i++) {
    
    if (fertility[i] > 0) {
      const uint ti = target[i-1];
      uint tclass = target_class_[ti];

      const Math1D::Vector<double>& cur_dict = dict_[ti];

      const int first_j = aligned_source_words[i][0];

      prob *= cur_dict[lookup(first_j,i-1)];

      //handle the head of the cept
      if (prev_cept_center != -1) {

        //DEBUG
        if (isnan(prob))
          std::cerr << "prob nan after dict-prob, pc != -1, i=" << i << std::endl;
        //END_DEBUG


        if (cept_start_mode_ != IBM4UNIFORM) {

	  const uint sclass = source_class_[source[first_j]];

	  if (inter_dist_mode_ == IBM4InterDistModePrevious)
	    tclass = target_class_[target[prev_cept-1]];

	  prob *= inter_distortion_prob(first_j,prev_cept_center,sclass,tclass,curJ);

          //DEBUG
          if (isnan(prob))
            std::cerr << "prob nan after inter-distort prob, i=" << i << std::endl;
          //END_DEBUG
        }
        else
          prob /= curJ;
      }
      else {

        if (use_sentence_start_prob_) {
          //DEBUG
          if (isnan(prob))
            std::cerr << "prob nan after dict-prob, pc == -1, i=" << i << std::endl;
          //END_DEBUG

          prob *= cur_sentence_start_prob[first_j];

          //DEBUG
          if (isnan(prob))
            std::cerr << "prob nan after sent start prob, pc == -1, i=" << i << std::endl;
          //END_DEBUG
        }
        else {

          //DEBUG
          if (isnan(prob))
            std::cerr << "prob nan after dict-prob, pc == -1, i=" << i << std::endl;
          //END_DEBUG

          prob *= 1.0 / curJ;
        }
      }

      //handle the body of the cept
      int prev_j = first_j;
      for (uint k=1; k < aligned_source_words[i].size(); k++) {

        const int cur_j = aligned_source_words[i][k];

	const uint cur_class = (intra_dist_mode_ == IBM4IntraDistModeSource) ? source_class_[source[cur_j]]
	  : target_class_[target[i-1]];

        prob *= cur_dict[lookup(cur_j,i-1)] * cur_intra_distortion_prob(cur_class,cur_j,prev_j);

        //std::cerr << "ap: tclass " << tclass << ", prob: " << cur_intra_distortion_prob(tclass,cur_j,prev_j) << std::endl;
	
        //DEBUG
        if (isnan(prob))
          std::cerr << "prob nan after combined body-prob, i=" << i << std::endl;
        //END_DEBUG

        prev_j = cur_j;
      }

      //compute the center of this cept and store the result in prev_cept_center

      switch (cept_start_mode_) {
      case IBM4CENTER : {
        double sum = 0.0;
        for (uint k=0; k < aligned_source_words[i].size(); k++)
          sum += aligned_source_words[i][k];

        prev_cept_center = (int) round(sum / fertility[i]);
        break;
      }
      case IBM4FIRST:
        prev_cept_center = first_j;
        break;
      case IBM4LAST: {
        prev_cept_center = prev_j; //was set to the last pos in the above loop
        break;
      }
      case IBM4UNIFORM:
        prev_cept_center = first_j; //will not be used
        break;
      default:
        assert(false);
      }

      prev_cept = i;

      assert(prev_cept_center >= 0);
    }
  }

  //handle empty word -- dictionary probs were handled above
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


long double IBM4Trainer::nondeficient_alignment_prob(const Storage1D<uint>& source, const Storage1D<uint>& target, 
                                                     const SingleLookupTable& cur_lookup, const Math1D::Vector<AlignBaseType>& alignment) {


  //this is exactly like for the IBM-3 (the difference is in the subroutine nondeficient_distortion_prob())
  
  long double prob = 1.0;

  const Storage1D<uint>& cur_source = source;
  const Storage1D<uint>& cur_target = target;

  const uint curI = cur_target.size();
  const uint curJ = cur_source.size();

  assert(alignment.size() == curJ);

  Math1D::NamedVector<uint> fertility(curI+1,0,MAKENAME(fertility));

  Storage1D<std::vector<AlignBaseType> > aligned_source_words(curI+1); //words are listed in ascending order
  
  for (uint j=0; j < curJ; j++) {
    const uint aj = alignment[j];
    fertility[aj]++;
    aligned_source_words[aj].push_back(j);
  }

  if (curJ < 2*fertility[0])
    return 0.0;

  for (uint i=1; i <= curI; i++) {
    uint t_idx = cur_target[i-1];
    //NOTE: no factorial here 
    prob *= fertility_prob_[t_idx][fertility[i]];
  }
  for (uint j=0; j < curJ; j++) {
    
    uint s_idx = cur_source[j];
    uint aj = alignment[j];
    
    if (aj == 0)
      prob *= dict_[0][s_idx-1];
    else {
      uint t_idx = cur_target[aj-1];
      prob *= dict_[t_idx][cur_lookup(j,aj-1)]; 
    }
  }

  prob *= nondeficient_distortion_prob(source,target,aligned_source_words);

  //handle empty word
  assert(fertility[0] <= 2*curJ);
  
  prob *= ldchoose(curJ-fertility[0],fertility[0]);
  for (uint k=1; k <= fertility[0]; k++)
    prob *= p_zero_;
  for (uint k=1; k <= curJ-2*fertility[0]; k++)
    prob *= p_nonzero_;

  return prob;
}

long double IBM4Trainer::distortion_prob(const Storage1D<uint>& source, const Storage1D<uint>& target, 
					 const Math1D::Vector<AlignBaseType>& alignment) {

  const uint curI = target.size();
  const uint curJ = source.size();

  assert(alignment.size() == curJ);

  
  NamedStorage1D<std::vector<AlignBaseType> > aligned_source_words(curI+1,MAKENAME(aligned_source_words));

  for (uint j=0; j < curJ; j++) {
    const uint aj = alignment[j];
    aligned_source_words[aj].push_back(j);
  }

  return distortion_prob(source,target,aligned_source_words);
}


//NOTE: the vectors need to be sorted
long double IBM4Trainer::distortion_prob(const Storage1D<uint>& source, const Storage1D<uint>& target, 
					 const Storage1D<std::vector<AlignBaseType> >& aligned_source_words) {

  long double prob = 1.0;

  const uint curI = target.size();
  const uint curJ = source.size();

  const Math3D::Tensor<float>& cur_intra_distortion_prob =  intra_distortion_prob_[curJ];

  const Math1D::Vector<double>& cur_sentence_start_prob = sentence_start_prob_[curJ];

  
  if (curJ < 2*aligned_source_words[0].size())
    return 0.0;

  //handle cepts with one or more aligned source words
  int prev_cept_center = -1;
  int prev_cept = -1;

  for (uint i=1; i <= curI; i++) {

    const std::vector<AlignBaseType>& cur_aligned_source_words = aligned_source_words[i];

    if (cur_aligned_source_words.size() > 0) {

      const uint ti = target[i-1];
      uint tclass = target_class_[ti];

      const int first_j = cur_aligned_source_words[0];

      //handle the head of the cept
      if (prev_cept_center != -1) {

        if (cept_start_mode_ != IBM4UNIFORM) {

          const uint sclass = source_class_[source[first_j]];

	  if (inter_dist_mode_ == IBM4InterDistModePrevious)
	    tclass = target_class_[target[prev_cept-1]];

          prob *= inter_distortion_prob(first_j,prev_cept_center,sclass,tclass,curJ);
        }
        else
          prob /= curJ;
      }
      else {
        if (use_sentence_start_prob_) {
          prob *= cur_sentence_start_prob[first_j];
        }
        else {
          prob *= 1.0 / curJ;
        }
      }

      //handle the body of the cept
      int prev_j = first_j;
      for (uint k=1; k < cur_aligned_source_words.size(); k++) {

        const int cur_j = cur_aligned_source_words[k];

	const uint cur_class = (intra_dist_mode_ == IBM4IntraDistModeSource) ? source_class_[source[cur_j]]
	  : target_class_[target[i-1]];

        prob *= cur_intra_distortion_prob(cur_class,cur_j,prev_j);

        prev_j = cur_j;
      }

      switch (cept_start_mode_) {
      case IBM4CENTER : {

	//compute the center of this cept and store the result in prev_cept_center
	double sum = 0.0;
	for (uint k=0; k < cur_aligned_source_words.size(); k++) {
	  sum += aligned_source_words[i][k];
	}

        prev_cept_center = (int) round(sum / cur_aligned_source_words.size());
        break;
      }
      case IBM4FIRST:
        prev_cept_center = first_j;
        break;
      case IBM4LAST:
        prev_cept_center = prev_j; //was set to the last position in the above loop
        break;
      case IBM4UNIFORM:
        prev_cept_center = first_j;
        break;
      default:
        assert(false);
      }

      prev_cept = i;
      assert(prev_cept_center >= 0);
    }
  }


  return prob;
}

//NOTE: the vectors need to be sorted
long double IBM4Trainer::nondeficient_distortion_prob(const Storage1D<uint>& source, const Storage1D<uint>& target, 
                                                      const Storage1D<std::vector<AlignBaseType> >& aligned_source_words) {


  long double prob = 1.0;

  const uint curI = target.size();
  const uint curJ = source.size();

  const Math1D::Vector<double>& cur_sentence_start_prob = sentence_start_prob_[curJ];
  
  if (curJ < 2*aligned_source_words[0].size())
    return 0.0;

  //handle cepts with one or more aligned source words
  int prev_cept_center = -1;
  int prev_cept = -1;

  Storage1D<bool> fixed(curJ,false);

  
  for (uint i=1; i <= curI; i++) {

    const std::vector<AlignBaseType>& cur_aligned_source_words = aligned_source_words[i];

    if (cur_aligned_source_words.size() > 0) {

      const uint ti = target[i-1];
      uint tclass = target_class_[ti];

      const int first_j = cur_aligned_source_words[0];

      uint nToRemove = cur_aligned_source_words.size()-1;

      //handle the head of the cept
      if (prev_cept_center != -1) {

        if (cept_start_mode_ != IBM4UNIFORM) {

          const uint sclass = source_class_[source[first_j]];

	  if (inter_dist_mode_ == IBM4InterDistModePrevious)
	    tclass = target_class_[target[prev_cept-1]];

          double num = cept_start_prob_(sclass,tclass,first_j-prev_cept_center+displacement_offset_);
          double denom = 0.0;

#if 0
	  //this version is several orders of magnitude slower
	  std::vector<uint> open_pos;
	  open_pos.reserve(curJ);
          for (int j=0; j < int(curJ); j++) {
            if (!fixed[j])
	      open_pos.push_back(j);
	  }
          if (nToRemove > 0) 
	    open_pos.resize(open_pos.size()-nToRemove);

	  for (uint k=0; k < open_pos.size(); k++)
	    denom += cept_start_prob_(sclass,tclass,open_pos[k]-prev_cept_center+displacement_offset_); 
#else
          for (int j=0; j < int(curJ); j++) {
            if (!fixed[j])
              denom += cept_start_prob_(sclass,tclass,j-prev_cept_center+displacement_offset_);
          }

          if (nToRemove > 0) {
            uint nRemoved = 0;
            for (int jj = curJ-1; jj >= 0; jj--) {
              if (!fixed[jj]) {
                denom -= cept_start_prob_(sclass,tclass,jj-prev_cept_center+displacement_offset_);
                nRemoved++;
                if (nRemoved == nToRemove)
                  break;
              }
            }
            assert(nRemoved == nToRemove);
          }
#endif
          prob *= num / denom;
        }
        else
          prob /= curJ;
      }
      else {
        if (use_sentence_start_prob_) {
          prob *= cur_sentence_start_prob[first_j];
        }
        else {
          prob *= 1.0 / curJ;
        }
      }
      fixed[first_j] = true;  

      //handle the body of the cept
      int prev_j = first_j;
      for (uint k=1; k < cur_aligned_source_words.size(); k++) {

        nToRemove--;

        const int cur_j = cur_aligned_source_words[k];
	
	const uint cur_class = (intra_dist_mode_ == IBM4IntraDistModeSource) ? source_class_[source[cur_j]]
	  : target_class_[target[i-1]];

        double num = within_cept_prob_(cur_class,cur_j-prev_j);

        double denom = 0.0;
#if 0
	//this version is several orders of magnitude slower
	std::vector<uint> open_pos;
	open_pos.reserve(curJ);
	for (int j=prev_j+1; j < int(curJ); j++) {
	  if (!fixed[j])
	    open_pos.push_back(j);
	}
	if (nToRemove > 0) 
	  open_pos.resize(open_pos.size()-nToRemove);
	
	for (uint k=0; k < open_pos.size(); k++)
	  denom += within_cept_prob_(cur_class,open_pos[k]-prev_j);
#else

        for (uint j=prev_j+1; j < curJ; j++) {
          denom += within_cept_prob_(cur_class,j-prev_j);
        }

        if (nToRemove > 0) {

          uint nRemoved = 0;
          for (int jj = curJ-1; jj >= 0; jj--) {
            if (!fixed[jj]) {
              denom -= within_cept_prob_(cur_class,jj-prev_j);
              nRemoved++;
              if (nRemoved == nToRemove)
                break;
            }
          }
          assert(nRemoved == nToRemove);
        }
#endif

        prob *= num / denom;

        fixed[cur_j] = true;

        prev_j = cur_j;
      }

      switch (cept_start_mode_) {
      case IBM4CENTER : {

	//compute the center of this cept and store the result in prev_cept_center
	double sum = 0.0;
	for (uint k=0; k < cur_aligned_source_words.size(); k++) {
	  sum += cur_aligned_source_words[k];
	}

        prev_cept_center = (int) round(sum / cur_aligned_source_words.size());
        break;
      }
      case IBM4FIRST:
        prev_cept_center = first_j;
        break;
      case IBM4LAST:
        prev_cept_center = prev_j; //was set to the last position in the above loop
        break;
      case IBM4UNIFORM:
        prev_cept_center = first_j; //will not be used
        break;
      default:
        assert(false);
      }

      prev_cept = i;
      assert(prev_cept_center >= 0);
    }
  }

  return prob;
}

void IBM4Trainer::print_alignment_prob_factors(const Storage1D<uint>& source, const Storage1D<uint>& target, 
					       const SingleLookupTable& cur_lookup, const Math1D::Vector<AlignBaseType>& alignment) {


  long double prob = 1.0;

  const uint curI = target.size();
  const uint curJ = source.size();

  assert(alignment.size() == curJ);

  Math1D::NamedVector<uint> fertility(curI+1,0,MAKENAME(fertility));
  NamedStorage1D<std::set<int> > aligned_source_words(curI+1,MAKENAME(aligned_source_words));

  const Math3D::Tensor<float>& cur_intra_distortion_prob =  intra_distortion_prob_[curJ];

  const Math1D::Vector<double>& cur_sentence_start_prob = sentence_start_prob_[curJ];
  
  for (uint j=0; j < curJ; j++) {
    const uint aj = alignment[j];
    aligned_source_words[aj].insert(j);
    fertility[aj]++;
    
    if (aj == 0) {
      prob *= dict_[0][source[j]-1];

      std::cerr << "mult by dict-prob for empty word, factor: " << dict_[0][source[j]-1] 
		<< ", result: " << prob << std::endl;
    }
  }
  
  if (curJ < 2*fertility[0]) {

    std::cerr << "ERROR: too many zero-aligned words, returning 0.0" << std::endl;
    return;
  }

  for (uint i=1; i <= curI; i++) {
    uint t_idx = target[i-1];
    prob *= fertility_prob_[t_idx][fertility[i]];

    std::cerr << "mult by fert-prob " << fertility_prob_[t_idx][fertility[i]] 
	      << ", result: " << prob << std::endl;

    if (!no_factorial_) {
      prob *= ld_fac_[fertility[i]]; 

      std::cerr << "mult by factorial " << ld_fac_[fertility[i]] 
		<< ", result: " << prob << std::endl;
    }
  }


  //handle cepts with one or more aligned source words
  int prev_cept_center = -1;
  int prev_cept = -1;

  for (uint i=1; i <= curI; i++) {

    if (fertility[i] > 0) {
      const uint ti = target[i-1];
      uint tclass = target_class_[ti];

      const int first_j = *aligned_source_words[i].begin();

      //handle the head of the cept
      if (prev_cept_center != -1) {

        const int first_j = *aligned_source_words[i].begin();
        prob *= dict_[ti][cur_lookup(first_j,i-1)];

	std::cerr << "mult by dict-prob " << dict_[ti][cur_lookup(first_j,i-1)] 
		  << ", result: " << prob << std::endl;


        if (cept_start_mode_ != IBM4UNIFORM) {

          const uint sclass = source_class_[source[first_j]];

	  if (inter_dist_mode_ == IBM4InterDistModePrevious)
	    tclass = target_class_[target[prev_cept-1]];

          prob *= inter_distortion_prob(first_j,prev_cept_center,sclass,tclass,curJ);

	  std::cerr << "mult by distortion-prob " << inter_distortion_prob(first_j,prev_cept_center,sclass,tclass,curJ)
                    << ", result: " << prob << std::endl;

        }
        else {
          prob /= curJ;

	  std::cerr << "div by " << curJ << ", result: " << prob << std::endl;
	}
      }
      else {
        if (use_sentence_start_prob_) {
          prob *= dict_[ti][cur_lookup(first_j,i-1)];

	  std::cerr << "mult by dict-prob " << dict_[ti][cur_lookup(first_j,i-1)]
		    << ", result: " << prob << std::endl;

          prob *= cur_sentence_start_prob[first_j];

	  std::cerr << "mult by start prob " << cur_sentence_start_prob[first_j] 
		    << ", result: " << prob << std::endl;
        }
        else {
          prob *= dict_[ti][cur_lookup(first_j,i-1)];

	  std::cerr << "mult by dict-prob " << dict_[ti][cur_lookup(first_j,i-1)]
		    << ", result: " << prob << std::endl;

          prob *= 1.0 / curJ;

	  std::cerr << "div by " << curJ << ", result: " << prob << std::endl;
        }
      }

      //handle the body of the cept
      int prev_j = first_j;
      std::set<int>::iterator ait = aligned_source_words[i].begin();
      for (++ait; ait != aligned_source_words[i].end(); ait++) {

        const int cur_j = *ait;

	const uint cur_class = (intra_dist_mode_ == IBM4IntraDistModeSource) ? source_class_[source[cur_j]]
	  : target_class_[target[i-1]];

        prob *= dict_[ti][cur_lookup(cur_j,i-1)] * cur_intra_distortion_prob(cur_class,cur_j,prev_j);
	
	std::cerr << "mult by dict-prob " << dict_[ti][cur_lookup(cur_j,i-1)] << " and distortion-prob "
		  << cur_intra_distortion_prob(cur_class,cur_j,prev_j) << ", result: " << prob 
		  << ", target index: " << ti << ", source index: " << cur_lookup(cur_j,i-1) << std::endl;

        prev_j = cur_j;
      }

      //compute the center of this cept and store the result in prev_cept_center

      switch (cept_start_mode_) {
      case IBM4CENTER : {
        
        double sum = 0.0;
        for (std::set<int>::iterator ait = aligned_source_words[i].begin(); ait != aligned_source_words[i].end(); ait++) {
          sum += *ait;
        }
        prev_cept_center = (int) round(sum / fertility[i]);
        break;
      }
      case IBM4FIRST:
        prev_cept_center = first_j;
        break;
      case IBM4LAST: {
        prev_cept_center = prev_j; //was set to the last position in the above loop
        break;
      }
      case IBM4UNIFORM:
        prev_cept_center = first_j; //will not be used
        break;
      default:
        assert(false);
      }

      prev_cept = i;
      assert(prev_cept_center >= 0);
    }
  }

  //handle empty word
  assert(fertility[0] <= 2*curJ);

  //dictionary probs were handled above
  
  prob *= ldchoose(curJ-fertility[0],fertility[0]);

  std::cerr << "mult by ldchoose " << ldchoose(curJ-fertility[0],fertility[0]) << ", result: " << prob << std::endl;

  for (uint k=1; k <= fertility[0]; k++) {
    prob *= p_zero_;

    std::cerr << "mult by p0 " << p_zero_ << ", result: " << prob << std::endl;
  }
  for (uint k=1; k <= curJ-2*fertility[0]; k++) {
    prob *= p_nonzero_;

    std::cerr << "mult by p1 " << p_nonzero_ << ", result: " << prob << std::endl;
  }

  if (och_ney_empty_word_) {

    for (uint k=1; k<= fertility[0]; k++) {
      prob *= ((long double) k) / curJ;

      std::cerr << "mult by k/curJ = " << (((long double) k) / curJ) << ", result: " << prob << std::endl;
    }
  }
}

//compact form
double IBM4Trainer::nondeficient_inter_m_step_energy(const IBM4CeptStartModel& singleton_count,
						     const std::vector<Math1D::Vector<uchar,uchar> >& open_pos,
						     const std::vector<double>& norm_weight,
						     const IBM4CeptStartModel& param, uint sclass, uint tclass) {



  double energy = 0.0;

  assert(singleton_count.zDim() == param.zDim());

  // a) singleton terms
  for (uint diff = 0; diff < singleton_count.zDim(); diff++)
    energy -= singleton_count(sclass,tclass,diff) * std::log(param(sclass,tclass,diff));

  // b) normalization terms
  //NOTE: here we don't need to consider the displacement offset, it is included in the values in count already
  
  for (uint k=0; k < norm_weight.size(); k++) {

    const Math1D::Vector<uchar,uchar>& open_diffs = open_pos[k];
    double weight = norm_weight[k];

    double sum = 0.0;
    for (uchar i=0; i < open_diffs.size(); i++)
      sum += param(sclass,tclass,open_diffs[i]);

    energy += weight * std::log(sum);
  }
  
  return energy;
}

//compact form with interpolation
double IBM4Trainer::nondeficient_inter_m_step_energy(const Math1D::Vector<double>& singleton_count,
						     const std::vector<double>& norm_weight,
						     const Math1D::Vector<double>& param1, const Math1D::Vector<double>& param2, 
						     const Math1D::Vector<double>& sum1, const Math1D::Vector<double>& sum2, 
						     double lambda) {


  const double neg_lambda = 1.0 - lambda;

  double energy = 0.0;

  // a) singleton terms
  for (uint diff = 0; diff < singleton_count.size(); diff++) {

    const double cur_param = lambda * param2[diff] +  neg_lambda * param1[diff];
    energy -= singleton_count[diff] * std::log(cur_param);
  }

  // b) normalization terms
  //NOTE: here we don't need to consider the displacement offset, it is included in the values in count already
  
  for (uint k=0; k < norm_weight.size(); k++) {

    const double weight = norm_weight[k];

    double sum = lambda * sum2[k] + neg_lambda * sum1[k];

    energy += weight * std::log(sum);
  }
  
  return energy;
}

//compact form
void IBM4Trainer::nondeficient_inter_m_step_with_interpolation(const IBM4CeptStartModel& singleton_count,
							       const std::vector<Math1D::Vector<uchar,uchar> >& open_diff,
							       const std::vector<double>& norm_weight,
							       uint sclass, uint tclass, double start_energy) {


  Math1D::Vector<double> relevant_singleton_count(singleton_count.zDim());
  for (uint diff = 0; diff < singleton_count.zDim(); diff++)
    relevant_singleton_count[diff] = singleton_count(sclass,tclass,diff);

  double energy = start_energy; 

  if (nSourceClasses_ * nTargetClasses_ <= 4)
    std::cerr << "start energy: " << energy << std::endl;

  Math1D::Vector<double> cur_cept_start_prob(cept_start_prob_.zDim());
  Math1D::Vector<double> gradient(cept_start_prob_.zDim());

  Math1D::Vector<double> new_cept_start_prob(cept_start_prob_.zDim());

  //test if normalizing the passed singleton counts gives a better starting point

  double rel_sum = relevant_singleton_count.sum();
  if (rel_sum > 1e-305) {

    IBM4CeptStartModel hyp_cept_start_prob(nSourceClasses_, nTargetClasses_, singleton_count.zDim(),MAKENAME(hyp_cept_start_prob));

    for (uint diff = 0; diff < singleton_count.zDim(); diff++) 
      hyp_cept_start_prob(sclass,tclass,diff) = std::max(1e-15,relevant_singleton_count[diff] / rel_sum);

    double hyp_energy = nondeficient_inter_m_step_energy(singleton_count,open_diff,norm_weight,hyp_cept_start_prob,sclass,tclass);

    if (hyp_energy < energy) {

      for (uint diff = 0; diff < singleton_count.zDim(); diff++) 
	cept_start_prob_(sclass,tclass,diff) = hyp_cept_start_prob(sclass,tclass,diff);
      
      if (nSourceClasses_ * nTargetClasses_ <= 4)
	std::cerr << "switching to passed normalized singleton count ---> " << hyp_energy << std::endl;

      energy = hyp_energy;
    }
  }

  double save_energy = energy;


  double alpha = 0.01;
  double line_reduction_factor = 0.35;

  for (uint k=0; k < cept_start_prob_.zDim(); k++) {
    cur_cept_start_prob[k] = std::max(1e-15,cept_start_prob_(sclass,tclass,k));
  }

  Math1D::Vector<double> sum(norm_weight.size());
  Math1D::Vector<double> new_sum(norm_weight.size(),0.0);

  for (uint k=0; k < norm_weight.size(); k++) {

    double cur_sum = 0.0;

    const Math1D::Vector<uchar,uchar>& open_diffs = open_diff[k];

    for (uchar i=0; i < open_diffs.size(); i++)
      cur_sum += cur_cept_start_prob[open_diffs[i]];

    sum[k] = cur_sum;
  }

  for (uint iter = 1; iter <= 250 /*400*/; iter++) {

    if ((iter%50) == 0) {
      if (nSourceClasses_ * nTargetClasses_ <= 4)
        std::cerr << "inter energy after iter #" << iter << ": " << energy << std::endl;


      if (save_energy - energy < 0.15)
        break;
      if (iter >= 100 && save_energy - energy < 0.5)
        break;

      save_energy = energy;
    }

    gradient.set_constant(0.0);

    /*** compute the gradient ***/

    // a) singleton terms
    for (uint diff = 0; diff < relevant_singleton_count.size(); diff++) {

      const double cur_param = cur_cept_start_prob[diff];
      assert(cur_param >= 1e-15);
      gradient[diff] -= relevant_singleton_count[diff] / cur_param;
    }

    //b) normalization terms
    for (uint k=0; k < norm_weight.size(); k++) {

      const Math1D::Vector<uchar,uchar>& open_diffs = open_diff[k];
      double weight = norm_weight[k];
      
      const uint size = open_diffs.size();

      const double addon = weight / sum[k];
      for (uchar i=0; i < size; i++)
        gradient[open_diffs[i]] += addon;
    }

    /*** go in neg. gradient direction ***/
    Math1D::Vector<double> temp(gradient.size());

    for (uint i=0; i < gradient.size(); i++) 
      temp[i] = cur_cept_start_prob[i] - alpha * gradient[i];

    /*** reproject ***/
    projection_on_simplex(temp.direct_access(), gradient.size());

    for (uint i=0; i < gradient.size(); i++) 
      new_cept_start_prob[i] = std::max(1e-15,temp[i]);

    for (uint k=0; k < norm_weight.size(); k++) {
      
      double cur_sum = 0.0;
    
      const Math1D::Vector<uchar,uchar>& open_diffs = open_diff[k];
      
      for (uchar i=0; i < open_diffs.size(); i++)
	cur_sum += new_cept_start_prob[open_diffs[i]];
      
      new_sum[k] = cur_sum;
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

      double hyp_energy = nondeficient_inter_m_step_energy(relevant_singleton_count, norm_weight, cur_cept_start_prob, new_cept_start_prob,
							   sum,new_sum,lambda);

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
      if (nSourceClasses_ * nTargetClasses_ <= 4)
	std::cerr << "CUTOFF after " << iter << " iterations" << std::endl;
      break;
    }

    if (nIter > 6)
      line_reduction_factor *= 0.9;

    double neg_best_lambda = 1.0 - best_lambda;

    for (uint k=0; k < gradient.size(); k++)
      cur_cept_start_prob[k] = std::max(1e-15,best_lambda * new_cept_start_prob[k] 
					+ neg_best_lambda * cur_cept_start_prob[k]);

    for (uint k=0; k < norm_weight.size(); k++) {
      
      sum[k] = best_lambda * new_sum[k] + neg_best_lambda * sum[k];
    }

    energy = best_energy;
  }


  //copy back
  for (uint k=0; k < gradient.size(); k++) {

    cept_start_prob_(sclass,tclass,k) = cur_cept_start_prob[k];
    assert(cept_start_prob_(sclass,tclass,k) >= 1e-15);
  }

}

//compact form
double IBM4Trainer::nondeficient_intra_m_step_energy(const IBM4WithinCeptModel& singleton_count,
						     const std::vector<std::pair<Math1D::Vector<uchar,uchar>,double> >& count,
						     const IBM4WithinCeptModel& param, uint sclass) {


  double energy = 0.0;

  // a) singleton terms
  for (uint k=1; k < singleton_count.yDim(); k++) {

    const double cur_param = param(sclass,k);
    assert(cur_param >= 1e-15);

    energy -= singleton_count(sclass,k) * std::log(cur_param);
  }

  // b) normalization terms
  for (uint k=0; k < count.size(); k++) {

    const Math1D::Vector<uchar,uchar>& open_diffs = count[k].first;
    double weight = count[k].second;

    double sum = 0.0;
    for (uchar i=0; i < open_diffs.size(); i++)
      sum += param(sclass,open_diffs[i]);

    energy += weight * std::log(sum);
  }
  
  return energy;
}

//compact form
void IBM4Trainer::nondeficient_intra_m_step(const IBM4WithinCeptModel& singleton_count,
					    const std::vector<std::pair<Math1D::Vector<uchar,uchar>,double> >& count, uint sclass) {



  for (uint k=1; k < within_cept_prob_.yDim(); k++)
    within_cept_prob_(sclass,k) = std::max(1e-15,within_cept_prob_(sclass,k));

  double energy = nondeficient_intra_m_step_energy(singleton_count,count,within_cept_prob_,sclass);

  if (nTargetClasses_ <= 4)
    std::cerr << "start energy: " << energy << std::endl;

  Math1D::Vector<double> gradient(within_cept_prob_.yDim());

  Math1D::Vector<double> new_within_cept_prob(within_cept_prob_.yDim());
  IBM4WithinCeptModel hyp_within_cept_prob = within_cept_prob_;

  //test if normalizing the passed singleton count gives a better starting point
  double rel_sum = 0.0;
  for (uint k=1; k < within_cept_prob_.yDim(); k++)
    rel_sum += singleton_count(sclass,k);

  if (rel_sum > 1e-305) {

    for (uint k=1; k < within_cept_prob_.yDim(); k++)
      hyp_within_cept_prob(sclass,k) = std::max(1e-15,singleton_count(sclass,k) / rel_sum);

    double hyp_energy = nondeficient_intra_m_step_energy(singleton_count,count,hyp_within_cept_prob,sclass);

    if (hyp_energy < energy) {

      for (uint k=1; k < within_cept_prob_.yDim(); k++)
	within_cept_prob_(sclass,k) = hyp_within_cept_prob(sclass,k);

      if (nTargetClasses_ <= 4)
	std::cerr << "switching to passed normalized count ----> " << hyp_energy << std::endl;

      energy = hyp_energy;
    }
  }
  

  double save_energy = energy;

  double alpha = 0.01;
  double line_reduction_factor = 0.35;

  for (uint iter = 1; iter <= 250 /*400*/; iter++) {

    gradient.set_constant(0.0);

    if ((iter%50) == 0) {
      if (nTargetClasses_ <= 4) 
        std::cerr << "intra energy after iter #" << iter << ": " << energy << std::endl;

      if (save_energy - energy < 0.15)
        break;
      if (iter >= 100 && save_energy - energy < 0.5)
        break;

      save_energy = energy;
    }

    /*** compute the gradient ***/

    // a) singleton terms
    for (uint k=1; k < singleton_count.yDim(); k++)
      gradient[k] -= singleton_count(sclass,k) /within_cept_prob_(sclass,k);

    // b) normalization terms
    for (uint k=0; k < count.size(); k++) {

      const Math1D::Vector<uchar,uchar>& open_diffs = count[k].first;
      double weight = count[k].second;
      
      double sum = 0.0;
      for (uchar i=0; i < open_diffs.size(); i++)
        sum += within_cept_prob_(sclass,open_diffs[i]);
      
      const double addon = weight / sum;
      for (uchar i=0; i < open_diffs.size(); i++)
        gradient[open_diffs[i]] += addon;
    }

    /*** go in neg. gradient direction ***/
    for (uint i=1; i < gradient.size(); i++) 
      new_within_cept_prob[i] = within_cept_prob_(sclass,i) - alpha * gradient[i];

    /*** reproject ***/
    projection_on_simplex(new_within_cept_prob.direct_access()+1, gradient.size()-1);
    
    for (uint i=1; i < gradient.size(); i++) 
      new_within_cept_prob[i] = std::max(1e-15, new_within_cept_prob[i]);

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

      for (uint k=0; k < gradient.size(); k++)
        hyp_within_cept_prob(sclass,k) = std::max(1e-15,lambda * new_within_cept_prob[k] 
						  + neg_lambda * within_cept_prob_(sclass,k));

      double hyp_energy = nondeficient_intra_m_step_energy(singleton_count, count, hyp_within_cept_prob,sclass);

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
      if (nTargetClasses_ <= 4)
	std::cerr << "CUTOFF after " << iter << " iterations" << std::endl;
      break;
    }

    if (nIter > 6)
      line_reduction_factor *= 0.9;

    double neg_best_lambda = 1.0 - best_lambda;

    for (uint k=0; k < gradient.size(); k++)
      within_cept_prob_(sclass,k) = std::max(1e-15,best_lambda * new_within_cept_prob[k] 
					     + neg_best_lambda * within_cept_prob_(sclass,k));

    energy = best_energy;
  }

  //DEBUG
  for (uint k=0; k < gradient.size(); k++)
    assert(within_cept_prob_(sclass,k) >= 1e-15);
  //END_DEBUG
}

long double IBM4Trainer::update_alignment_by_hillclimbing(const Storage1D<uint>& source, const Storage1D<uint>& target, 
                                                          const SingleLookupTable& lookup, uint& nIter, Math1D::Vector<uint>& fertility,
                                                          Math2D::Matrix<long double>& expansion_prob,
                                                          Math2D::Matrix<long double>& swap_prob, Math1D::Vector<AlignBaseType>& alignment) {

   if (nondeficient_) {

    return nondeficient_hillclimbing(source,target,lookup,nIter,fertility,expansion_prob,swap_prob,alignment);
  }
 
  const double improvement_factor = 1.001;

  const uint curI = target.size();
  const uint curJ = source.size(); 

  //std::cerr << "*************** hillclimb: J = " << curJ << ", I=" << curI << std::endl;
  //std::cerr << "start alignment: " << alignment << std::endl;


  fertility.resize(curI+1);

  long double base_prob = alignment_prob(source,target,lookup,alignment);

  //DEBUG
  if (isnan(base_prob) || isinf(base_prob) || base_prob <= 0.0) {
    std::cerr << "ERROR: base_prob in hillclimbing is " << base_prob << std::endl;
    print_alignment_prob_factors(source, target, lookup, alignment);
    exit(1);
  }
  //END_DEBUG

  swap_prob.resize(curJ,curJ);
  expansion_prob.resize(curJ,curI+1);

  uint count_iter = 0;

  const Math3D::Tensor<float>& cur_intra_distortion_prob =  intra_distortion_prob_[curJ];
  const Math1D::Vector<double>& cur_sentence_start_prob = sentence_start_prob_[curJ];

  //source words are listed in ascending order
  NamedStorage1D< std::vector<AlignBaseType> > aligned_source_words(curI+1,MAKENAME(aligned_source_words));
  
  fertility.set_constant(0);
  for (uint j=0; j < curJ; j++) {
    const uint aj = alignment[j];
    fertility[aj]++;
    aligned_source_words[aj].push_back(j);
  }

  long double base_distortion_prob = distortion_prob(source,target,aligned_source_words);


  while (true) {    

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
          cept_center[i] = (int) round(sum_j / aligned_source_words[i].size());
          break;
        }
        case IBM4FIRST:
          cept_center[i] = aligned_source_words[i][0];
          break;
        case IBM4LAST:
          cept_center[i] = aligned_source_words[i].back();
          break;
        case IBM4UNIFORM:
          break;
        default:
          assert(false);
        }

        prev_i = i;
      }
    }

    count_iter++;
    nIter++;

    //std::cerr << "****************** starting new hillclimb iteration, current best prob: " << base_prob << std::endl;

    bool improvement = false;

    long double best_prob = base_prob;
    bool best_change_is_move = false;
    uint best_move_j = MAX_UINT;
    uint best_move_aj = MAX_UINT;
    uint best_swap_j1 = MAX_UINT;
    uint best_swap_j2 = MAX_UINT;

    /**** scan neighboring alignments and keep track of the best one that is better 
     ****  than the current alignment  ****/

    //std::clock_t tStartExp,tEndExp;
    //tStartExp = std::clock();

    //a) expansion moves

    NamedStorage1D< std::vector<AlignBaseType> > hyp_aligned_source_words(MAKENAME(hyp_aligned_source_words));
    hyp_aligned_source_words = aligned_source_words;

    for (uint j=0; j < curJ; j++) {

      const uint aj = alignment[j];
      assert(fertility[aj] > 0);
      expansion_prob(j,aj) = 0.0;

      const uint s_idx = source[j];

      const uint j_class = source_class_[s_idx];

      const uint prev_ti = (aj == 0) ? 0 : target[aj-1];
      const uint prev_ti_class = (prev_ti == 0) ? MAX_UINT : target_class_[prev_ti];
      const double old_dict_prob = (aj == 0) ? dict_[0][s_idx-1] : dict_[prev_ti][lookup(j,aj-1)];
      
      const uint prev_aj_fert = fertility[aj];

      const uint prev_i = prev_cept[aj];
      const uint next_i = next_cept[aj];

      //std::cerr << "j: " << j << ", aj: " << aj << std::endl;

      for (uint cand_aj = 0; cand_aj <= curI; cand_aj++) {
      
        if (cand_aj != aj) {

	  //std::cerr << "cand_aj: " << cand_aj << std::endl;

          long double hyp_prob = 0.0;

          bool incremental_calculation = false;

	  const uint new_ti = (cand_aj == 0) ? 0 : target[cand_aj-1];
	  const uint new_ti_class = (new_ti == 0) ? MAX_UINT : target_class_[new_ti];
          const double new_dict_prob = (cand_aj == 0) ? dict_[0][s_idx-1] : dict_[new_ti][lookup(j,cand_aj-1)];

	  //EXPERIMENTAL (prune constellations with very unlikely translation probs.)
          if (new_dict_prob < 1e-10) {
            expansion_prob(j,cand_aj) = 0.0;
            continue;
          }
	  //END_EXPERIMENTAL
	  if (cand_aj != 0 && (fertility[cand_aj]+1) > fertility_limit_) {

            expansion_prob(j,cand_aj) = 0.0;
            continue;
	  }
	  else if (cand_aj == 0 && curJ < 2*fertility[0]+2) {

            expansion_prob(j,cand_aj) = 0.0;
            continue;
	  }

          long double incoming_prob = new_dict_prob; 
          long double leaving_prob = old_dict_prob; 

	  if (cand_aj != 0)
            incoming_prob *= fertility_prob_[new_ti][fertility[cand_aj]+1];
	  if (aj != 0)
	    incoming_prob *= fertility_prob_[prev_ti][fertility[aj]-1];
	    
	  if (!no_factorial_) {
	    if (cand_aj != 0)
	      incoming_prob *= ld_fac_[fertility[cand_aj]+1]; 
	    if (aj != 0)
	      incoming_prob *= ld_fac_[fertility[aj]-1]; 
	  }
	  
	  if (cand_aj != 0)
	    leaving_prob *= fertility_prob_[new_ti][fertility[cand_aj]];
	  if (aj != 0)
	    leaving_prob *= fertility_prob_[prev_ti][fertility[aj]];
	  
	  assert(leaving_prob > 0.0);
	  
	  if (!no_factorial_) {
	    if (cand_aj != 0)
	      leaving_prob *= ld_fac_[fertility[cand_aj]]; 
	    if (aj != 0)
	      leaving_prob *= ld_fac_[fertility[aj]]; 
	  }



          if (aj != 0 && cand_aj != 0) {

            if (next_i != MAX_UINT &&
                (((prev_i != MAX_UINT && cand_aj < prev_i) || (prev_i == MAX_UINT && cand_aj > next_i)) 
                 || ((next_i != MAX_UINT && cand_aj > next_i) 
                     || (next_i == MAX_UINT && prev_i != MAX_UINT && cand_aj < prev_i)   )  ) ) {

              incremental_calculation = true;

              /***************************** 1. changes regarding aj ******************************/
              if (prev_aj_fert > 1) {
                //the cept aj remains

                uint jnum;
                for (jnum = 0; jnum < prev_aj_fert; jnum++) {
                  if (aligned_source_words[aj][jnum] == j)
                    break;
                }

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
                  new_aj_center = (int) round(sum_j / (aligned_source_words[aj].size()-1));
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
		
                //re-calculate the transition aj -> next_i
                if (next_i != MAX_UINT && new_aj_center != cept_center[aj]) { 
		  const uint sclass = source_class_[source[aligned_source_words[next_i][0]]];
		  const uint tclass = (inter_dist_mode_ == IBM4InterDistModePrevious) ? target_class_[prev_ti] 
		    : target_class_[target[next_i-1]];

                  leaving_prob *= inter_distortion_prob(aligned_source_words[next_i][0],cept_center[aj],sclass,tclass,curJ);

                  assert(leaving_prob > 0.0);

                  incoming_prob *= inter_distortion_prob(aligned_source_words[next_i][0],new_aj_center,sclass,tclass,curJ);
                }

                if (jnum == 0) {
                  //the transition prev_i -> aj is affected

                  if (prev_i != MAX_UINT) {
                    const uint old_sclass = j_class; 
		    const uint new_sclass = source_class_[source[aligned_source_words[aj][1]]];

		    const uint tclass = (inter_dist_mode_ == IBM4InterDistModePrevious) ? target_class_[target[prev_i-1]]
		      : prev_ti_class; 

                    leaving_prob *= inter_distortion_prob(j,cept_center[prev_i],old_sclass,tclass,curJ);
                    incoming_prob *= inter_distortion_prob(aligned_source_words[aj][1],cept_center[prev_i],new_sclass,tclass,curJ);
                  }
                  else if (use_sentence_start_prob_) {
                    leaving_prob *= cur_sentence_start_prob[j];
                    incoming_prob *= cur_sentence_start_prob[aligned_source_words[aj][1]];
                  }

                  assert(leaving_prob > 0.0);

		  const uint cur_class = (intra_dist_mode_ == IBM4IntraDistModeSource) 
		    ? source_class_[source[aligned_source_words[aj][1]]] : prev_ti_class; 

                  leaving_prob *= cur_intra_distortion_prob(cur_class,aligned_source_words[aj][1],j);

                  assert(leaving_prob > 0.0);
                }
                else {
		  
		  const uint cur_class = (intra_dist_mode_ == IBM4IntraDistModeSource) 
		    ? source_class_[source[aligned_source_words[aj][jnum]]] : prev_ti_class; 

                  leaving_prob *= cur_intra_distortion_prob(cur_class,aligned_source_words[aj][jnum],aligned_source_words[aj][jnum-1]);

                  assert(leaving_prob > 0.0);

                  if (jnum+1 < prev_aj_fert) {

		    const uint cur_class = (intra_dist_mode_ == IBM4IntraDistModeSource) 
		      ? source_class_[source[aligned_source_words[aj][jnum+1]]] : prev_ti_class; 

                    leaving_prob *= cur_intra_distortion_prob(cur_class,aligned_source_words[aj][jnum+1], aligned_source_words[aj][jnum]);

                    assert(leaving_prob > 0.0);

                    incoming_prob *= cur_intra_distortion_prob(cur_class,aligned_source_words[aj][jnum+1],aligned_source_words[aj][jnum-1]);
                  }
                }

              }
              else {
                //the cept aj vanishes

                //erase the transitions prev_i -> aj    and    aj -> next_i
                if (prev_i != MAX_UINT) {
		  const uint sclass = j_class; 

		  const uint tclass = (inter_dist_mode_ == IBM4InterDistModePrevious) ? target_class_[target[prev_i-1]]
		    : prev_ti_class; 

                  leaving_prob *= inter_distortion_prob(j,cept_center[prev_i],sclass,tclass,curJ);

                  assert(leaving_prob > 0.0);
                }		
                else if (use_sentence_start_prob_) {
                  leaving_prob *= cur_sentence_start_prob[j];

                  assert(leaving_prob > 0.0);
                }

                if (next_i != MAX_UINT) {
                  const uint sclass = source_class_[source[aligned_source_words[next_i][0]]];
		  const uint tclass = (inter_dist_mode_ == IBM4InterDistModePrevious) ? prev_ti_class
		    : target_class_[target[next_i-1]];

                  leaving_prob *= inter_distortion_prob(aligned_source_words[next_i][0],j,sclass,tclass,curJ);

                  assert(leaving_prob > 0.0);
                }
                
                //introduce the transition prev_i -> next_i
                if (prev_i != MAX_UINT) {
                  if (next_i != MAX_UINT) {
                    const uint sclass = source_class_[source[aligned_source_words[next_i][0]]];
		    const uint tclass = (inter_dist_mode_ == IBM4InterDistModePrevious) ? target_class_[target[prev_i-1]]
		      : target_class_[target[next_i-1]];
		    
                    incoming_prob *= inter_distortion_prob(aligned_source_words[next_i][0],cept_center[prev_i],sclass,tclass,curJ);
                  }
                }
                else if (use_sentence_start_prob_)
                  incoming_prob *= cur_sentence_start_prob[aligned_source_words[next_i][0]];
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

              if (fertility[cand_aj] > 0) {
                //the cept cand_aj was already there

                uint insert_pos = 0;
                for (; insert_pos < fertility[cand_aj] 
                       && aligned_source_words[cand_aj][insert_pos] < j; insert_pos++) {
                  //empty body
                }

                if (insert_pos == 0) {

                  if (cand_prev_i == MAX_UINT) {

                    if (use_sentence_start_prob_) {
                      leaving_prob *= cur_sentence_start_prob[aligned_source_words[cand_aj][0]];
                      incoming_prob *= cur_sentence_start_prob[j];

                      assert(leaving_prob > 0.0);
		    }
		     
		    const uint cur_class = (intra_dist_mode_ == IBM4IntraDistModeSource) ?
		      source_class_[source[aligned_source_words[cand_aj][0]]] : new_ti_class; 

		    incoming_prob *= cur_intra_distortion_prob(cur_class,aligned_source_words[cand_aj][0],j);
                  }
                  else {
                    const uint old_sclass = source_class_[source[aligned_source_words[cand_aj][0]]];
		    const uint tclass = (inter_dist_mode_ == IBM4InterDistModePrevious) ? target_class_[target[cand_prev_i-1]]
		      : new_ti_class;

                    leaving_prob *= inter_distortion_prob(aligned_source_words[cand_aj][0],cept_center[cand_prev_i],old_sclass,tclass,curJ);

                    assert(leaving_prob > 0.0);

		    const uint new_sclass = j_class;

                    incoming_prob *= inter_distortion_prob(j,cept_center[cand_prev_i],new_sclass,tclass,curJ);

		    const uint cur_class = (intra_dist_mode_ == IBM4IntraDistModeSource) ? old_sclass
		      : new_ti_class;

                    incoming_prob *= cur_intra_distortion_prob(cur_class,aligned_source_words[cand_aj][0],j);
                  }
                }
                else if (insert_pos < fertility[cand_aj]) {
		  
		  const uint cur_class = (intra_dist_mode_ == IBM4IntraDistModeSource) ?
		    source_class_[source[ aligned_source_words[cand_aj][insert_pos] ]] : new_ti_class;

                  leaving_prob *= cur_intra_distortion_prob(cur_class,aligned_source_words[cand_aj][insert_pos],
                                                            aligned_source_words[cand_aj][insert_pos-1]);

                  assert(leaving_prob > 0.0);

		  const uint new_sclass = (intra_dist_mode_ == IBM4IntraDistModeSource) 
		    ? j_class : cur_class;

                  incoming_prob *= cur_intra_distortion_prob(new_sclass,j,aligned_source_words[cand_aj][insert_pos-1]);
                  incoming_prob *= cur_intra_distortion_prob(cur_class,aligned_source_words[cand_aj][insert_pos],j);
                }
                else {
                  //insert at the end
                  assert(insert_pos == fertility[cand_aj]);

		  const uint cur_class = (intra_dist_mode_ == IBM4IntraDistModeSource)  ? 
		    source_class_[source[j]] : new_ti_class;

                  incoming_prob *= cur_intra_distortion_prob(cur_class,j,aligned_source_words[cand_aj][insert_pos-1]);
                }

                if (cand_next_i != MAX_UINT) {
                  //calculate new center of cand_aj

                  uint new_cand_aj_center = MAX_UINT;
                  switch (cept_start_mode_) {
                  case IBM4CENTER : {
                    double sum_j = j;
                    for (uint k=0; k < fertility[cand_aj]; k++)
                      sum_j += aligned_source_words[cand_aj][k];

                    new_cand_aj_center = (int) round(sum_j / (fertility[cand_aj]+1) );
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
                    if (insert_pos >= fertility[cand_aj])
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
		  
                  if (new_cand_aj_center != cept_center[cand_aj] && cept_center[cand_aj] != new_cand_aj_center) {
                    const uint sclass = source_class_[source[aligned_source_words[cand_next_i][0]]];
		    const uint tclass = (inter_dist_mode_ == IBM4InterDistModePrevious) ? new_ti_class
		      : target_class_[target[cand_next_i-1]];

                    leaving_prob *= inter_distortion_prob(aligned_source_words[cand_next_i][0],cept_center[cand_aj],sclass,tclass,curJ);
		    
                    assert(leaving_prob > 0.0);

                    incoming_prob *= inter_distortion_prob(aligned_source_words[cand_next_i][0],new_cand_aj_center,sclass,tclass,curJ);
                  }
                }
              }
              else {
                //the cept cand_aj is newly created

                //erase the transition cand_prev_i -> cand_next_i (if existent)
                if (cand_prev_i != MAX_UINT && cand_next_i != MAX_UINT) {

                  const uint sclass = source_class_[source[aligned_source_words[cand_next_i][0]]];
		  const uint tclass = (inter_dist_mode_ == IBM4InterDistModePrevious) ? target_class_[target[cand_prev_i-1]]
		    : target_class_[target[cand_next_i-1]];

                  leaving_prob *= inter_distortion_prob(aligned_source_words[cand_next_i][0],cept_center[cand_prev_i],sclass,tclass,curJ);

                  assert(leaving_prob > 0.0);
                }
                else if (cand_prev_i == MAX_UINT) {
		  
                  assert(cand_next_i != MAX_UINT);
                  if (use_sentence_start_prob_)
                    leaving_prob *= cur_sentence_start_prob[aligned_source_words[cand_next_i][0]];

                  assert(leaving_prob > 0.0);
                }
                else {
                  //nothing to do here
                }

                //introduce the transitions cand_prev_i -> cand_aj    and   cand_aj -> cand_next_i
                if (cand_prev_i != MAX_UINT) {
                  const uint sclass = j_class;
		  const uint tclass = (inter_dist_mode_ == IBM4InterDistModePrevious) ? target_class_[target[cand_prev_i-1]]
		    : target_class_[new_ti];

                  incoming_prob *= inter_distortion_prob(j,cept_center[cand_prev_i],sclass,tclass,curJ);
                }
                else
                  incoming_prob *= cur_sentence_start_prob[j];

                if (cand_next_i != MAX_UINT) {
                  const uint sclass = source_class_[source[aligned_source_words[cand_next_i][0]]];
		  const uint tclass = (inter_dist_mode_ == IBM4InterDistModePrevious) ? new_ti_class
		    : target_class_[target[cand_next_i-1]];

                  incoming_prob *= inter_distortion_prob(aligned_source_words[cand_next_i][0],j,sclass,tclass,curJ);
                }
              }

              assert(leaving_prob > 0.0);

              hyp_prob = base_prob * incoming_prob / leaving_prob;

	      if (isnan(hyp_prob)) {

    		std::cerr << "######in exp. move for j=" << j << " cur aj: " << aj << ", candidate: " << cand_aj << std::endl;

		std::cerr << "hyp_prob: " << hyp_prob << std::endl;
		std::cerr << "base: " << base_prob << ", incoming: " << incoming_prob << ", leaving: " << leaving_prob << std::endl;
		std::cerr << "hc iter: " << count_iter << std::endl;
		exit(1);
	      }
	      
#ifndef NDEBUG
              //DEBUG
              Math1D::Vector<AlignBaseType> hyp_alignment = alignment;
              hyp_alignment[j] = cand_aj;
              long double check_prob = alignment_prob(source,target,lookup,hyp_alignment);

              if (check_prob > 0.0) {
		
                long double check_ratio = hyp_prob / check_prob;
		
                if (! (check_ratio > 0.99 && check_ratio < 1.01)) {

                  std::cerr << "****************************************************************" << std::endl;
                  std::cerr << "expansion: moving j=" << j << " to cand_aj=" << cand_aj << " from aj=" << aj
                            << std::endl;
                  std::cerr << "curJ: " << curJ << ", curI: " << curI << std::endl;
                  std::cerr << "base alignment: " << alignment << std::endl;
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
          } //end -- if (aj != 0 && cand_aj != 0)
          else if (cand_aj == 0) {
            //NOTE: this time we also handle the cases where next_i == MAX_UINT or where prev_i == MAX_UINT

            incremental_calculation = true;

            assert(aj != 0);

            const uint prev_zero_fert = fertility[0];
            const uint new_zero_fert = prev_zero_fert+1;

            if (curJ < 2*new_zero_fert) {
              hyp_prob = 0.0;
            }
            else {

	      incoming_prob *= (curJ-2*prev_zero_fert) * (curJ - 2*prev_zero_fert-1) * p_zero_; 
	      leaving_prob *= ((curJ-prev_zero_fert) * new_zero_fert * p_nonzero_ * p_nonzero_);

	      if (och_ney_empty_word_) 
		incoming_prob *= (prev_zero_fert+1) / ((long double) curJ);

              if (prev_aj_fert > 1 ) {
                //the cept aj remains

                uint jnum;
                for (jnum = 0; jnum < prev_aj_fert; jnum++) {
                  if (aligned_source_words[aj][jnum] == j)
                    break;
                }

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

                  //re-calculate the transition aj -> next_i
                  if (cept_center[aj] != new_aj_center) {
		    const uint sclass = source_class_[source[aligned_source_words[next_i][0]]];
		    const uint tclass = (inter_dist_mode_ == IBM4InterDistModePrevious) ? prev_ti_class
		      : target_class_[target[next_i-1]];
                    
                    leaving_prob *= inter_distortion_prob(aligned_source_words[next_i][0],cept_center[aj],sclass,tclass,curJ);

                    incoming_prob *= inter_distortion_prob(aligned_source_words[next_i][0],new_aj_center,sclass,tclass,curJ);
                  }
                }

                if (jnum == 0) {
                  //the transition prev_i -> aj is affected

                  if (prev_i != MAX_UINT) {
                    const uint old_sclass = j_class;

		    const uint tclass = (inter_dist_mode_ == IBM4InterDistModePrevious) ? target_class_[target[prev_i-1]]
		      : prev_ti_class; 

                    leaving_prob *= inter_distortion_prob(j,cept_center[prev_i],old_sclass,tclass,curJ);

                    const uint new_sclass = source_class_[source[aligned_source_words[aj][1]]];

                    incoming_prob *= inter_distortion_prob(aligned_source_words[aj][1],cept_center[prev_i],new_sclass,tclass,curJ);
                  }
                  else if (use_sentence_start_prob_) {
                    leaving_prob *= sentence_start_prob_[curJ][j];
                    incoming_prob *= sentence_start_prob_[curJ][aligned_source_words[aj][1]];
                  }

		  const uint cur_class = (intra_dist_mode_ == IBM4IntraDistModeSource) ? 
		    source_class_[source[aligned_source_words[aj][1]]] : prev_ti_class;

		  leaving_prob *= cur_intra_distortion_prob(cur_class,aligned_source_words[aj][1],j);
                }
                else {
		  
		  const uint cur_class = (intra_dist_mode_ == IBM4IntraDistModeSource) ? 
		    source_class_[source[aligned_source_words[aj][jnum]]] : prev_ti_class;

                  leaving_prob *= cur_intra_distortion_prob(cur_class,aligned_source_words[aj][jnum],aligned_source_words[aj][jnum-1]);

                  if (jnum+1 < prev_aj_fert) {

		    const uint cur_class = (intra_dist_mode_ == IBM4IntraDistModeSource) ? 
		      source_class_[source[aligned_source_words[aj][jnum+1]]] : prev_ti_class;

                    leaving_prob *= cur_intra_distortion_prob(cur_class,aligned_source_words[aj][jnum+1],aligned_source_words[aj][jnum]);
		    
                    incoming_prob *= cur_intra_distortion_prob(cur_class,aligned_source_words[aj][jnum+1],aligned_source_words[aj][jnum-1]);
                  }
                }
              }
              else {
                //the cept aj vanishes

                //erase the transitions prev_i -> aj    and    aj -> next_i
                if (prev_i != MAX_UINT) {
		  const uint tclass = (inter_dist_mode_ == IBM4InterDistModePrevious) ? target_class_[target[prev_i-1]] 
		    : prev_ti_class;
                  const uint sclass = j_class;

                  leaving_prob *= inter_distortion_prob(j,cept_center[prev_i],sclass,tclass,curJ);
                }
                else if (use_sentence_start_prob_) {
                  leaving_prob *= sentence_start_prob_[curJ][j];
                }

                if (next_i != MAX_UINT) {
                  const uint sclass = source_class_[source[aligned_source_words[next_i][0]]];
		  const uint tclass = (inter_dist_mode_ == IBM4InterDistModePrevious) ? prev_ti_class
		    : target_class_[target[next_i-1]];

                  leaving_prob *= inter_distortion_prob(aligned_source_words[next_i][0],j,sclass,tclass,curJ);
                  
                  //introduce the transition prev_i -> next_i
                  if (prev_i != MAX_UINT) {

		    const uint tclass = (inter_dist_mode_ == IBM4InterDistModePrevious) ? target_class_[target[prev_i-1]]
		      : target_class_[target[next_i-1]];

                    incoming_prob *= inter_distortion_prob(aligned_source_words[next_i][0],cept_center[prev_i],sclass,tclass,curJ);
                  }
                  else if (use_sentence_start_prob_) {
                    incoming_prob *= sentence_start_prob_[curJ][aligned_source_words[next_i][0]];
                  } 
                }
              }
	    
              assert(leaving_prob > 0.0);
              
              hyp_prob = base_prob * incoming_prob / leaving_prob;

#ifndef NDEBUG
              //DEBUG
              Math1D::Vector<AlignBaseType> hyp_alignment = alignment;
              hyp_alignment[j] = cand_aj;
              long double check_prob = alignment_prob(source,target,lookup,hyp_alignment);
	      
              if (check_prob != 0.0) {
                
                long double check_ratio = hyp_prob / check_prob;
		
                if (! (check_ratio > 0.99 && check_ratio < 1.01) ) {
                  //if (true) {
                  
                  std::cerr << "incremental prob: " << hyp_prob << std::endl;
                  std::cerr << "actual prob: " << check_prob << std::endl;

                  std::cerr << ", J=" << curJ << ", I=" << curI << std::endl;
                  std::cerr << "base alignment: " << alignment << std::endl;
                  std::cerr << "moving source word " << j << " from " << alignment[j] << " to 0"
                            << std::endl;
                }
		
                if (check_prob > 1e-12 * best_prob)
                  assert (check_ratio > 0.99 && check_ratio < 1.01);
              }
              //END_DEBUG
#endif
            }	      
          }

          if (!incremental_calculation) {

	    std::vector<AlignBaseType>::iterator it = std::find(hyp_aligned_source_words[aj].begin(),
                                                                hyp_aligned_source_words[aj].end(),j);
	    hyp_aligned_source_words[aj].erase(it);

	    hyp_aligned_source_words[cand_aj].push_back(j);
	    vec_sort(hyp_aligned_source_words[cand_aj]);

	    hyp_prob = base_prob * distortion_prob(source,target,hyp_aligned_source_words)
	      / base_distortion_prob;

            assert(cand_aj != 0); //since we handle that case above

	    uint prev_zero_fert = fertility[0];
	    uint new_zero_fert = prev_zero_fert;
	    
	    if (aj == 0) {
	      new_zero_fert--;
	    }
	    else if (cand_aj == 0) {
	      new_zero_fert++;
	    }

	    if (prev_zero_fert < new_zero_fert) {

	      incoming_prob *= (curJ-2*prev_zero_fert) * (curJ - 2*prev_zero_fert-1) * p_zero_; 
	      leaving_prob *= ((curJ-prev_zero_fert) * new_zero_fert * p_nonzero_ * p_nonzero_);

	      
	      if (och_ney_empty_word_) 
		incoming_prob *= (prev_zero_fert+1) / ((long double) curJ);
	    }
	    else if (prev_zero_fert > new_zero_fert) {
	      
	      incoming_prob *= (curJ-new_zero_fert) * prev_zero_fert * p_nonzero_ * p_nonzero_ ;
	      leaving_prob *= ((curJ-2*prev_zero_fert+1) * (curJ-2*new_zero_fert) * p_zero_);
	     

	      if (och_ney_empty_word_) 
		incoming_prob *= curJ / ((long double) prev_zero_fert);
	    }

	    hyp_prob *= incoming_prob / leaving_prob; 

	    //restore for next loop execution
	    hyp_aligned_source_words[aj] = aligned_source_words[aj];
	    hyp_aligned_source_words[cand_aj] = aligned_source_words[cand_aj];


	    //DEBUG
	    if (isnan(hyp_prob)) {
	      std::cerr << "incoming: " << incoming_prob << std::endl;
	      std::cerr << "leaving: " << leaving_prob << std::endl;
	    }
	    //END_DEBUG

#ifndef NDEBUG
            Math1D::Vector<AlignBaseType> hyp_alignment = alignment;
            hyp_alignment[j] = cand_aj;

	    long double check = alignment_prob(source,target,lookup,hyp_alignment);

	    if (check > 1e-250) {

	      if (! (check / hyp_prob <= 1.005 && check / hyp_prob >= 0.995)) {
		std::cerr << "j: " << j << ", aj: " << aj << ", cand_aj: " << cand_aj << std::endl;
		std::cerr << "calculated: " << hyp_prob << ", should be: " << check << std::endl;
                std::cerr << "base alignment: " << alignment << std::endl;

                std::cerr << "no_factorial_: " << no_factorial_ << std::endl;
                std::cerr << "prev_zero_fert: " << prev_zero_fert << ", new_zero_fert: " << new_zero_fert << std::endl;
	      }

	      assert(check / hyp_prob <= 1.005);
	      assert(check / hyp_prob >= 0.995);
	    }
	    else if (check > 0.0) {

	      if (! (check / hyp_prob <= 1.5 && check / hyp_prob >= 0.666)) {
		std::cerr << "aj: " << aj << ", cand_aj: " << cand_aj << std::endl;
		std::cerr << "calculated: " << hyp_prob << ", should be: " << check << std::endl;
	      }

	      assert(check / hyp_prob <= 1.5);
	      assert(check / hyp_prob >= 0.666);

	    }
	    else
	      assert(hyp_prob == 0.0);
#endif
          }
	    
          expansion_prob(j,cand_aj) = hyp_prob;

          if (isnan(expansion_prob(j,cand_aj)) || isinf(expansion_prob(j,cand_aj))) {

            std::cerr << "nan/inf in exp. move for j=" << j << ", " << aj << " -> " << cand_aj 
		      << ": " << expansion_prob(j,cand_aj) << std::endl;
            std::cerr << "current alignment: " << aj << std::endl;
            std::cerr << "curJ: " << curJ << ", curI: " << curI << std::endl;
            std::cerr << "incremental calculation: " << incremental_calculation << std::endl;

            Math1D::Vector<AlignBaseType> hyp_alignment = alignment;
            hyp_alignment[j] = cand_aj;
	    std::cerr << "prob. of start alignment: " 
		      << alignment_prob(source,target,lookup,alignment) << std::endl;

            std::cerr << "check prob: " << alignment_prob(source,target,lookup,hyp_alignment) << std::endl;	    
	    std::cerr << "base distortion prob: " << base_distortion_prob << std::endl;
	    std::cerr << "check-hyp distortion prob: " << distortion_prob(source,target,hyp_alignment) << std::endl;

	    print_alignment_prob_factors(source,target,lookup,alignment);
          }

          assert(!isnan(expansion_prob(j,cand_aj)));
          assert(!isinf(expansion_prob(j,cand_aj)));
	  
          if (hyp_prob > improvement_factor*best_prob) {
	    
            best_prob = hyp_prob;
            improvement = true;
            best_change_is_move = true;
            best_move_j = j;
            best_move_aj = cand_aj;
          }
        }    
      }
    }

    //tEndExp = std::clock();
    //std::cerr << " spent " << diff_seconds(tEndExp,tStartExp) << " seconds on expansion moves" << std::endl;

    //std::clock_t tStartSwap,tEndSwap;
    //tStartSwap = std::clock();

    //std::cerr << "starting with swap moves" << std::endl;

    //b) swap moves
    for (uint j1=0; j1 < curJ; j1++) {

      //std::cerr << "j1: " << j1 << std::endl;
      
      const uint aj1 = alignment[j1];
      const uint taj1 = (aj1 > 0) ? target[aj1-1] : 0;

      for (uint j2 = j1+1; j2 < curJ; j2++) {

        //std::cerr << "j2: " << j2 << std::endl;

        const uint aj2 = alignment[j2];

        if (aj1 == aj2) {
          //we do not want to count the same alignment twice
          swap_prob(j1,j2) = 0.0;
        }
        else {

	  const uint taj2 = (aj2 > 0) ? target[aj2-1] : 0;

	  //EXPERIMENTAL (prune constellations with very unlikely translation probs.)
	  if (aj1 != 0) {
	    if (dict_[taj1][lookup(j2,aj1-1)] < 1e-10) {
	      swap_prob(j1,j2) = 0.0;
	      continue;
	    }
	  }
	  else {
	    if (dict_[0][source[j2]-1] < 1e-10) {
	      swap_prob(j1,j2) = 0.0;
	      continue;
	    }
	  }
	  if (aj2 != 0) {
	    if (dict_[taj2][lookup(j1,aj2-1)] < 1e-10) {
	      swap_prob(j1,j2) = 0.0;
	      continue;
	    }
	  }
	  else {
	    if (dict_[0][source[j1]-1] < 1e-10) {
	      swap_prob(j1,j2) = 0.0;
	      continue;
	    }
	  }
	  //END_EXPERIMENTAL


          long double hyp_prob = 0.0;

	  uint temp_aj1 = aj1;
	  uint temp_aj2 = aj2;
	  uint temp_j1 = j1;
	  uint temp_j2 = j2;
	  uint temp_taj1 = taj1;
	  uint temp_taj2 = taj2;
	  if (aj1 > aj2) {
	    temp_aj1 = aj2;
	    temp_aj2 = aj1;
	    temp_j1 = j2;
	    temp_j2 = j1;
	    temp_taj1 = taj2;
	    temp_taj2 = taj1;
	  }


	  long double incoming_prob; 
	  long double leaving_prob;
	  
	  if (aj1 != 0) {
	    leaving_prob = dict_[taj1][lookup(j1,aj1-1)];
	    incoming_prob = dict_[taj1][lookup(j2,aj1-1)];
	  }
	  else {
	    leaving_prob = dict_[0][source[j1]-1];
	    incoming_prob = dict_[0][source[j2]-1];
	  }
	  
	  if (aj2 != 0) {
	    leaving_prob *= dict_[taj2][lookup(j2,aj2-1)];
	    incoming_prob *= dict_[taj2][lookup(j1,aj2-1)];
	  }
	  else {
	    leaving_prob *= dict_[0][source[j2]-1];
	    incoming_prob *= dict_[0][source[j1]-1];
	  }
	  
	  assert(leaving_prob > 0.0);


	  const uint temp_j1_class = source_class_[source[temp_j1]];
	  const uint temp_j2_class = source_class_[source[temp_j2]];
	  const uint temp_taj1_class = (temp_taj1 == 0) ? MAX_UINT : target_class_[temp_taj1];
	  const uint temp_taj2_class = (temp_taj2 == 0) ? MAX_UINT : target_class_[temp_taj2];

          if (aj1 != 0 && aj2 != 0 && 
              cept_start_mode_ != IBM4UNIFORM &&
              aligned_source_words[aj1].size() == 1 && aligned_source_words[aj2].size() == 1) {
            //both affected cepts are one-word cepts

            // 1. entering cept temp_aj1
            if (prev_cept[temp_aj1] != MAX_UINT) {
              const uint old_sclass = temp_j1_class;
              const uint new_sclass = temp_j2_class;
              const uint tclass = (inter_dist_mode_ == IBM4InterDistModePrevious) ? target_class_[target[prev_cept[temp_aj1]-1]]
		: temp_taj1_class;

              leaving_prob *= inter_distortion_prob(temp_j1,cept_center[prev_cept[temp_aj1]],old_sclass,tclass,curJ);
              incoming_prob *= inter_distortion_prob(temp_j2,cept_center[prev_cept[temp_aj1]],new_sclass,tclass,curJ);
            }
            else if (use_sentence_start_prob_) {
              leaving_prob *= sentence_start_prob_[curJ][temp_j1];
              incoming_prob *= sentence_start_prob_[curJ][temp_j2];
            }

            // 2. leaving cept temp_aj1 and entering cept temp_aj2
            if (prev_cept[temp_aj2] != temp_aj1) {
	      
              //a) leaving cept aj1
              const uint next_i = next_cept[temp_aj1];
              if (next_i != MAX_UINT) {
		
		const uint sclass = source_class_[source[aligned_source_words[next_i][0]]];
                const uint tclass = (inter_dist_mode_ == IBM4InterDistModePrevious) ? temp_taj1_class
		  : target_class_[target[next_i-1]];

                leaving_prob *= inter_distortion_prob(aligned_source_words[next_i][0],temp_j1,sclass,tclass,curJ);

                incoming_prob *= inter_distortion_prob(aligned_source_words[next_i][0],temp_j2,sclass,tclass,curJ);
              }
	      
              //b) entering cept temp_aj2
	      const uint sclass1 = temp_j1_class;
              const uint sclass2 = temp_j2_class;
              const uint tclass = (inter_dist_mode_ == IBM4InterDistModePrevious) ? target_class_[target[prev_cept[temp_aj2]-1]]
		: temp_taj2_class;

              leaving_prob *= inter_distortion_prob(temp_j2,cept_center[prev_cept[temp_aj2]],sclass2,tclass,curJ);
              incoming_prob *= inter_distortion_prob(temp_j1,cept_center[prev_cept[temp_aj2]],sclass1,tclass,curJ);
            }
            else {
              //leaving cept temp_aj1 is simultaneously entering cept temp_aj2
              //NOTE: the aligned target word is here temp_aj2-1 in both the incoming and the leaving term

              const uint sclass1 = temp_j1_class;
              const uint tclass = (inter_dist_mode_ == IBM4InterDistModePrevious) ? temp_taj1_class
		: temp_taj2_class;
              const uint sclass2 = temp_j2_class;

              leaving_prob *= inter_distortion_prob(temp_j2,temp_j1,sclass2,tclass,curJ);

              incoming_prob *= inter_distortion_prob(temp_j1,temp_j2,sclass1,tclass,curJ);
            }
	    
            // 3. leaving cept temp_aj2
            if (next_cept[temp_aj2] != MAX_UINT) {
	      const uint tclass = (inter_dist_mode_ == IBM4InterDistModePrevious) ? temp_taj2_class
		: target_class_[target[next_cept[temp_aj2]-1]];

	      const uint sclass = source_class_[source[aligned_source_words[next_cept[temp_aj2]][0]]];

              leaving_prob *= inter_distortion_prob(aligned_source_words[next_cept[temp_aj2]][0],temp_j2,sclass,tclass,curJ);

              incoming_prob *= inter_distortion_prob(aligned_source_words[next_cept[temp_aj2]][0],temp_j1,sclass,tclass,curJ);
            }
	  
            hyp_prob = base_prob * incoming_prob / leaving_prob;

#ifndef NDEBUG
            //DEBUG
            Math1D::Vector<AlignBaseType> hyp_alignment = alignment;
            hyp_alignment[j1] = aj2;
            hyp_alignment[j2] = aj1;
            long double check_prob = alignment_prob(source,target,lookup,hyp_alignment);
            
            if (check_prob > 0.0) {
              
              long double check_ratio = check_prob / hyp_prob;
              
              if (! (check_ratio > 0.99 && check_ratio < 1.01)) {
                
                std::cerr << "******* swapping " << j1 << "->" << aj1 << " and " << j2 << "->" << aj2 << std::endl;
                std::cerr << " curJ: " << curJ << ", curI: " << curI << std::endl;
                std::cerr << "base alignment: " << alignment << std::endl;
                std::cerr << "actual prob: " << check_prob << std::endl;
                std::cerr << "incremental_hyp_prob: " << hyp_prob << std::endl;
                
              }
              
              assert(check_ratio > 0.99 && check_ratio < 1.01);
            }
            //END_DEBUG
#endif
          }
          else if (aj1 != 0 && aj2 != 0 && 
                   prev_cept[aj1] != aj2 && prev_cept[aj2] != aj1) {

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

            std::vector<AlignBaseType> new_temp_aj1_aligned_source_words = aligned_source_words[temp_aj1];
            new_temp_aj1_aligned_source_words[old_j1_num] = temp_j2;
	    vec_sort(new_temp_aj1_aligned_source_words);

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
              new_temp_aj1_center = new_temp_aj1_aligned_source_words.back();
              break;
            }
            case IBM4UNIFORM : {
              break;
            }
            default: assert(false);
            }

            const int old_head1 = aligned_source_words[temp_aj1][0];
            const int new_head1 = new_temp_aj1_aligned_source_words[0];

            if (old_head1 != new_head1) {
              if (prev_cept[temp_aj1] != MAX_UINT) {
		const uint old_sclass = source_class_[source[old_head1]];
		const uint new_sclass = source_class_[source[new_head1]];
                const uint tclass = (inter_dist_mode_ == IBM4InterDistModePrevious) ? target_class_[target[prev_cept[temp_aj1]-1]]
		  : temp_taj1_class;

                leaving_prob *= inter_distortion_prob(old_head1,cept_center[prev_cept[temp_aj1]],old_sclass,tclass,curJ);
                incoming_prob *= inter_distortion_prob(new_head1,cept_center[prev_cept[temp_aj1]],new_sclass,tclass,curJ);
              }
              else if (use_sentence_start_prob_) {
                leaving_prob *= sentence_start_prob_[curJ][old_head1];
                incoming_prob *= sentence_start_prob_[curJ][new_head1];
              }
            }

            for (uint k=1; k < fertility[temp_aj1]; k++) {
	      const uint old_class = (intra_dist_mode_ == IBM4IntraDistModeSource) ? 
		source_class_[source[aligned_source_words[temp_aj1][k]]] : temp_taj1_class;
	      const uint new_class = (intra_dist_mode_ == IBM4IntraDistModeSource) ? 
		source_class_[source[new_temp_aj1_aligned_source_words[k]]] : temp_taj1_class;
              leaving_prob *= cur_intra_distortion_prob(old_class,aligned_source_words[temp_aj1][k],aligned_source_words[temp_aj1][k-1]);
              incoming_prob *= cur_intra_distortion_prob(new_class,new_temp_aj1_aligned_source_words[k], 
                                                         new_temp_aj1_aligned_source_words[k-1]);
            }

            //transition to next cept
            if (next_cept[temp_aj1] != MAX_UINT) {
              const int next_head = aligned_source_words[next_cept[temp_aj1]][0];
	      
	      const uint sclass = source_class_[source[next_head]];
              const uint tclass = (inter_dist_mode_ == IBM4InterDistModePrevious) ? temp_taj1_class
		: target_class_[target[next_cept[temp_aj1]-1]];

              leaving_prob *= inter_distortion_prob(next_head,cept_center[temp_aj1],sclass,tclass,curJ);

              incoming_prob *= inter_distortion_prob(next_head,new_temp_aj1_center,sclass,tclass,curJ);
            }

            std::vector<AlignBaseType> new_temp_aj2_aligned_source_words = aligned_source_words[temp_aj2];
            new_temp_aj2_aligned_source_words[old_j2_num] = temp_j1;
	    vec_sort(new_temp_aj2_aligned_source_words);

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
              new_temp_aj2_center = new_temp_aj2_aligned_source_words.back();
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
                const uint old_sclass = source_class_[source[old_head2]];
                const uint new_sclass = source_class_[source[new_head2]];
                const uint tclass = (inter_dist_mode_ == IBM4InterDistModePrevious) ? target_class_[target[prev_cept[temp_aj2]-1]]
		  : temp_taj2_class;

                leaving_prob *= inter_distortion_prob(old_head2,cept_center[prev_cept[temp_aj2]],old_sclass,tclass,curJ);
                incoming_prob *= inter_distortion_prob(new_head2,cept_center[prev_cept[temp_aj2]],new_sclass,tclass,curJ);
              }
              else if (use_sentence_start_prob_) {
                leaving_prob *= sentence_start_prob_[curJ][old_head2];
                incoming_prob *= sentence_start_prob_[curJ][new_head2];
              }
            }
	    
            for (uint k=1; k < fertility[temp_aj2]; k++) {
	      const uint old_class = (intra_dist_mode_ == IBM4IntraDistModeSource) ? 
		source_class_[source[aligned_source_words[temp_aj2][k]]] : temp_taj2_class;
	      const uint new_class = (intra_dist_mode_ == IBM4IntraDistModeSource) ? 
		source_class_[source[new_temp_aj2_aligned_source_words[k]]] : temp_taj2_class;

              leaving_prob *= cur_intra_distortion_prob(old_class,aligned_source_words[temp_aj2][k],aligned_source_words[temp_aj2][k-1]);
              incoming_prob *= cur_intra_distortion_prob(new_class,new_temp_aj2_aligned_source_words[k],
                                                         new_temp_aj2_aligned_source_words[k-1]);
            }

            //transition to next cept
            if (next_cept[temp_aj2] != MAX_UINT && cept_center[temp_aj2] != new_temp_aj2_center) {
              const int next_head = aligned_source_words[next_cept[temp_aj2]][0];
	      
	      const uint sclass = source_class_[source[next_head]];
              const uint tclass = (inter_dist_mode_ == IBM4InterDistModePrevious) ? temp_taj2_class
		: target_class_[target[next_cept[temp_aj2]-1]];

              leaving_prob *= inter_distortion_prob(next_head,cept_center[temp_aj2],sclass,tclass,curJ);

              incoming_prob *= inter_distortion_prob(next_head,new_temp_aj2_center,sclass,tclass,curJ);
            }

            hyp_prob = base_prob * incoming_prob / leaving_prob;

#ifndef NDEBUG
            //DEBUG
            Math1D::Vector<AlignBaseType> hyp_alignment = alignment;
            hyp_alignment[j1] = aj2;
            hyp_alignment[j2] = aj1;
            long double check_prob = alignment_prob(source,target,lookup,hyp_alignment);
                    
            if (check_prob > 0.0) {
              
              long double check_ratio = check_prob / hyp_prob;
              
              if (! (check_ratio > 0.99 && check_ratio < 1.01)) {
                
                std::cerr << "******* swapping " << j1 << "->" << aj1 << " and " << j2 << "->" << aj2 << std::endl;
                std::cerr << "curJ: " << curJ << ", curI: " << curI << std::endl;
                std::cerr << "base alignment: " << alignment << std::endl;
                std::cerr << "actual prob: " << check_prob << std::endl;
                std::cerr << "incremental_hyp_prob: " << hyp_prob << std::endl;
                std::cerr << "(base prob: " << base_prob << ")" << std::endl;
              }
              
              if (check_prob > 1e-12*base_prob) 
                assert(check_ratio > 0.99 && check_ratio < 1.01);
            }
            //END_DEBUG
#endif

          }
          else {

	    std::vector<AlignBaseType>::iterator it = std::find(hyp_aligned_source_words[aj1].begin(),
                                                                hyp_aligned_source_words[aj1].end(),j1);
	    hyp_aligned_source_words[aj1].erase(it);
	    it = std::find(hyp_aligned_source_words[aj2].begin(),hyp_aligned_source_words[aj2].end(),j2);
	    hyp_aligned_source_words[aj2].erase(it);
	    hyp_aligned_source_words[aj1].push_back(j2);
	    hyp_aligned_source_words[aj2].push_back(j1);

	    vec_sort(hyp_aligned_source_words[aj1]);
	    vec_sort(hyp_aligned_source_words[aj2]);

	    hyp_prob = base_prob * distortion_prob(source,target,hyp_aligned_source_words)
	      / base_distortion_prob;

	    hyp_prob *= incoming_prob / leaving_prob; 

	    //restore for next loop execution:
	    hyp_aligned_source_words[aj1] = aligned_source_words[aj1];
	    hyp_aligned_source_words[aj2] = aligned_source_words[aj2];

#ifndef NDEBUG
            Math1D::Vector<AlignBaseType> hyp_alignment = alignment;
	    std::swap(hyp_alignment[j1],hyp_alignment[j2]);

	    long double check = alignment_prob(source,target,lookup,hyp_alignment);

	    if (check > 1e-250) {

	      if (! (check / hyp_prob <= 1.005 && check / hyp_prob >= 0.995)) {
		std::cerr << "aj1: " << aj1 << ", aj2: " << aj2 << std::endl;
		std::cerr << "calculated: " << hyp_prob << ", should be: " << check << std::endl;
	      }

	      assert(check / hyp_prob <= 1.005);
	      assert(check / hyp_prob >= 0.995);
	    }
	    else if (check > 0.0) {

	      if (! (check / hyp_prob <= 1.5 && check / hyp_prob >= 0.666)) {
		std::cerr << "aj1: " << aj1 << ", aj2: " << aj2 << std::endl;
		std::cerr << "calculated: " << hyp_prob << ", should be: " << check << std::endl;
	      }

	      assert(check / hyp_prob <= 1.5);
	      assert(check / hyp_prob >= 0.666);

	    }
	    else
	      assert(hyp_prob == 0.0);
#endif
          } //end of case 3


	  //DEBUG
	  if (isnan(hyp_prob) || isinf(hyp_prob)) {
	    std::cerr << "ERROR: swap prob in hillclimbing is " << hyp_prob << std::endl;
	    Math1D::Vector<AlignBaseType> temp_alignment = alignment;
	    std::swap(temp_alignment[j1],temp_alignment[j2]);
	    print_alignment_prob_factors(source, target, lookup, temp_alignment);
	    exit(1);
	  }
	  //END_DEBUG


          assert(!isnan(hyp_prob));

          swap_prob(j1,j2) = hyp_prob;

          if (hyp_prob > improvement_factor*best_prob) {
	    
            improvement = true;
            best_change_is_move = false;
            best_prob = hyp_prob;
            best_swap_j1 = j1;
            best_swap_j2 = j2;
          }
        }

        assert(!isnan(swap_prob(j1,j2)));
        assert(!isinf(swap_prob(j1,j2)));
      }
    }

    //tEndSwap = std::clock();

    //update alignment if a better one was found
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

      aligned_source_words[best_move_aj].push_back(best_move_j);
      vec_sort(aligned_source_words[best_move_aj]);
      
      std::vector<AlignBaseType>::iterator it = std::find(aligned_source_words[cur_aj].begin(),
                                                          aligned_source_words[cur_aj].end(),best_move_j);
      assert(it != aligned_source_words[cur_aj].end());

      aligned_source_words[cur_aj].erase(it);
    }
    else {
      //std::cerr << "swapping: j1=" << best_swap_j1 << std::endl;
      //std::cerr << "swapping: j2=" << best_swap_j2 << std::endl;

      uint cur_aj1 = alignment[best_swap_j1];
      uint cur_aj2 = alignment[best_swap_j2];

      assert(cur_aj1 != cur_aj2);
      
      alignment[best_swap_j1] = cur_aj2;
      alignment[best_swap_j2] = cur_aj1;

      //NOTE: the fertilities are not affected here
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
    }

    //std::cerr << "probability improved from " << base_prob << " to " << best_prob << std::endl;
    base_prob = best_prob;    

#ifndef NDEBUG
    double check_ratio = alignment_prob(source,target,lookup,alignment) / base_prob;

    if (base_prob > 1e-250) {

      if ( !(check_ratio >= 0.99 && check_ratio <= 1.01)) {
        std::cerr << "check: " << alignment_prob(source,target,lookup,alignment) << std::endl;;
      }

      assert(check_ratio >= 0.99 && check_ratio <= 1.01);
    }
#endif

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


long double IBM4Trainer::nondeficient_hillclimbing(const Storage1D<uint>& source, const Storage1D<uint>& target, 
                                                   const SingleLookupTable& lookup, uint& nIter, 
                                                   Math1D::Vector<uint>& fertility,
                                                   Math2D::Matrix<long double>& expansion_prob,
                                                   Math2D::Matrix<long double>& swap_prob, Math1D::Vector<AlignBaseType>& alignment) {

  //this is just like for the IBM-3, only a different distortion routine is called

  //std::cerr << "nondef hc" << std::endl;

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

  long double base_distortion_prob = nondeficient_distortion_prob(source,target,aligned_source_words);
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
  // long double check_prob = nondeficient_alignment_prob(source,target,lookup,alignment);
  // double check_ratio = base_prob / check_prob;
  // assert(check_ratio >= 0.99 && check_ratio <= 1.01);
  //END_DEBUG


  uint count_iter = 0;

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

      //std::cerr << "j: " << j << std::endl;

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
	if (cand_aj > 0) { //better to check this before computing distortion probs
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
          hyp_aligned_source_words[cand_aj].push_back(j);
	  vec_sort(hyp_aligned_source_words[cand_aj]);

          long double leaving_prob = base_distortion_prob * old_dict_prob;
          long double incoming_prob = nondeficient_distortion_prob(source,target,hyp_aligned_source_words)
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
          // long double cand_prob = nondeficient_alignment_prob(source,target,lookup,hyp_alignment);

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
          hyp_aligned_source_words[cand_aj] = aligned_source_words[cand_aj];
        }
      }

      hyp_aligned_source_words[aj] = aligned_source_words[aj];
    }

    /**** swap moves ****/
    for (uint j1=0; j1 < curJ; j1++) {

      //std::cerr << "j1: " << j1 << std::endl;
      
      const uint aj1 = alignment[j1];
      const uint s_j1 = source[j1];

      for (uint j2 = j1+1; j2 < curJ; j2++) {

        //std::cerr << "j2: " << j2 << std::endl;

        const uint aj2 = alignment[j2];
        const uint s_j2 = source[j2];

        if (aj1 == aj2) {
          //we do not want to count the same alignment twice
          swap_prob(j1,j2) = 0.0;
        }
        else {
          
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
            nondeficient_distortion_prob(source,target,hyp_aligned_source_words);

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
          // long double cand_prob = nondeficient_alignment_prob(source,target,lookup,hyp_alignment);          

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
    base_distortion_prob = nondeficient_distortion_prob(source,target,aligned_source_words);
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

/* virtual */
void IBM4Trainer::prepare_external_alignment(const Storage1D<uint>& source, const Storage1D<uint>& target,
					     const SingleLookupTable& lookup,
					     Math1D::Vector<AlignBaseType>& alignment) {

  common_prepare_external_alignment(source,target,lookup,alignment);

  const uint J = source.size();

  /*** check if respective distortion table is present. If not, create one from the parameters ***/

  int oldJ = (cept_start_prob_.zDim() + 1) / 2;

  bool update = false;

  if (oldJ < int(J)) {
    update = true;

    inter_distortion_cache_.resize(J+1);

    //inter params
    IBM4CeptStartModel new_param(cept_start_prob_.xDim(),cept_start_prob_.yDim(),2*J-1,1e-8,MAKENAME(new_param));
    uint new_zero_offset = J-1;
    for (int j = -int(maxJ_)+1; j <= int(maxJ_)-1; j++) {

      for (uint w1=0; w1 < cept_start_prob_.xDim(); w1++) 
        for (uint w2=0; w2 < cept_start_prob_.yDim(); w2++) 
          new_param(w1,w2,new_zero_offset + j) = cept_start_prob_(w1,w2,displacement_offset_ + j);

    }
    cept_start_prob_ = new_param;

    //intra params

    IBM4WithinCeptModel new_wi_model(within_cept_prob_.xDim(),J,1e-8,MAKENAME(new_wi_model)); 

    for (uint c=0; c < new_wi_model.xDim(); c++) {

      for (uint k=0; k < within_cept_prob_.yDim(); k++)
	new_wi_model(c,k) = within_cept_prob_(c,k);
    }

    within_cept_prob_ = new_wi_model;

    displacement_offset_ = new_zero_offset;

    maxJ_ = J;

    sentence_start_parameters_.resize(J,0.0);
  }

  if (inter_distortion_prob_.size() <= J) {
    update = true;
    inter_distortion_prob_.resize(J+1);
  }
  if (intra_distortion_prob_.size() <= J) {
    update = true;
    intra_distortion_prob_.resize(J+1);
  }
 
  uint max_s=0;
  uint max_t=0;
  for (uint s=0; s < source.size(); s++) {
    max_s = std::max<uint>(max_s,source_class_[source[s]]);
  }
  for (uint t=0; t < target.size(); t++) {
    max_t = std::max<uint>(max_t,target_class_[target[t]]);
  }


  inter_distortion_prob_[J].resize(std::max<uint>(inter_distortion_prob_[J].xDim(),max_s+1),
                                   std::max<uint>(inter_distortion_prob_[J].yDim(),max_t+1));

  uint dim = (intra_dist_mode_ == IBM4IntraDistModeTarget) ? max_t+1 : max_s+1;

  if (intra_distortion_prob_[J].xDim() <= dim) {
    update = true;
    intra_distortion_prob_[J].resize(dim,J,J);
  }

  if (use_sentence_start_prob_) {

    if (sentence_start_prob_.size() <= J) {
      update = true;
      sentence_start_prob_.resize(J+1);
    }

    if (sentence_start_prob_[J].size() < J) {
      update = true;
      sentence_start_prob_[J].resize(J);
    } 
  }  

  if (update) {
    par2nonpar_inter_distortion();

    par2nonpar_intra_distortion();
    if (use_sentence_start_prob_)  {
      par2nonpar_start_prob(sentence_start_parameters_,sentence_start_prob_);
    }
  }
}

DistortCount::DistortCount(uchar J, uchar j, uchar j_prev)
  : J_(J), j_(j), j_prev_(j_prev) {}

bool operator<(const DistortCount& d1, const DistortCount& d2) {
  if (d1.J_ != d2.J_)
    return (d1.J_ < d2.J_);
  if (d1.j_ != d2.j_)
    return (d1.j_ < d2.j_);
  return (d1.j_prev_ < d2.j_prev_);
}

void IBM4Trainer::train_unconstrained(uint nIter, FertilityModelTrainer* fert_trainer, HmmWrapper* wrapper) {

  std::cerr << "starting IBM-4 training without constraints";
  if (fert_trainer != 0)
    std::cerr << " (init from " << fert_trainer->model_name() <<  ") "; 
  else if (wrapper != 0)
    std::cerr << " (init from HMM) ";
  std::cerr << std::endl;

  Storage1D<Math1D::Vector<AlignBaseType> > initial_alignment;
  if (hillclimb_mode_ == HillclimbingRestart) 
    initial_alignment = best_known_alignment_; //CAUTION: we will save here the alignment from the IBM-3, NOT from the HMM

  double max_perplexity = 0.0;
  double approx_sum_perplexity = 0.0;

  IBM4CeptStartModel fceptstart_count(cept_start_prob_.xDim(),cept_start_prob_.yDim(),2*maxJ_-1,MAKENAME(fceptstart_count));
  IBM4WithinCeptModel fwithincept_count(within_cept_prob_.xDim(),within_cept_prob_.yDim(),MAKENAME(fwithincept_count));

  Storage1D<Storage2D<Math2D::Matrix<double> > > inter_distort_count(maxJ_+1);
  Storage1D<Math3D::Tensor<double> > intra_distort_count(maxJ_+1);
  
  Storage1D<Math1D::Vector<double> > sentence_start_count(maxJ_+1);

  SingleLookupTable aux_lookup;

  for (uint J=1; J <= maxJ_; J++) {

    if (reduce_deficiency_) {
      inter_distort_count[J].resize(inter_distortion_prob_[J].xDim(),inter_distortion_prob_[J].yDim());

      if (!nondeficient_) {

        for (uint x=0; x < inter_distortion_prob_[J].xDim(); x++)
          for (uint y=0; y < inter_distortion_prob_[J].yDim(); y++)
            inter_distort_count[J](x,y).resize(inter_distortion_prob_[J](x,y).xDim(),inter_distortion_prob_[J](x,y).yDim(),0.0);
      }

      intra_distort_count[J].resize(intra_distortion_prob_[J].xDim(),intra_distortion_prob_[J].yDim(),
                                    intra_distortion_prob_[J].zDim(),0.0);
    }
    
    sentence_start_count[J].resize(J,0);
  }

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

  double hillclimbtime = 0.0;
  double countcollecttime = 0.0;

  uint iter;
  for (iter=1+iter_offs_; iter <= nIter+iter_offs_; iter++) {

    Storage2D<std::map<DistortCount,double> > sparse_inter_distort_count(nSourceClasses_,nTargetClasses_);


    //NOTE: in the presence of many word classes using these counts is a waste of memory.
    // It would be more prudent to keep track of the counts for every sentence (using the CountStructure from the IBM3, 
    //  or maybe with maps replaced by vectors)
    // and to then filter out the relevant counts for the current combination. Only, that's much more complex to implement,
    // so it may take a while (or never be done)
    Storage2D<std::map<Math1D::Vector<uchar,uchar>,double> > nondef_cept_start_count(nSourceClasses_,nTargetClasses_); 
    Storage1D<std::map<Math1D::Vector<uchar,uchar>,double> > nondef_within_cept_count(nTargetClasses_); 

    //this count is almost like fceptstart_count, but only includes the terms > nondef_thresh and also no terms
    // where only one position is available
    IBM4CeptStartModel fnondef_ceptstart_singleton_count(cept_start_prob_.xDim(),cept_start_prob_.yDim(),2*maxJ_-1,0.0,
							 MAKENAME(fnondef_ceptstart_singleton_count));

    //same for this count and fnondef_withincept_count
    IBM4WithinCeptModel fnondef_withincept_singleton_count(within_cept_prob_.xDim(),within_cept_prob_.yDim(),0.0,
							   MAKENAME(fnondef_withincept_singleton_count));
    
    double nondef_thresh = 1e-6;

    std::cerr << "******* IBM-4 EM-iteration " << iter << std::endl;

    uint sum_iter = 0;

    fzero_count = 0.0;
    fnonzero_count = 0.0;

    fceptstart_count.set_constant(0.0);
    fwithincept_count.set_constant(0.0);

    for (uint i=0; i < nTargetWords; i++) {
      fwcount[i].set_constant(0.0);
      ffert_count[i].set_constant(0.0);
    }

    for (uint J=1; J <= maxJ_; J++) {
      if (inter_distort_count[J].size() > 0) {
        for (uint y=0; y < inter_distortion_prob_[J].yDim(); y++)
          for (uint x=0; x < inter_distortion_prob_[J].xDim(); x++)
            inter_distort_count[J](x,y).set_constant(0.0);
      }
      intra_distort_count[J].set_constant(0.0);
      sentence_start_count[J].set_constant(0.0);
    }

    max_perplexity = 0.0;
    approx_sum_perplexity = 0.0;

    for (size_t s=0; s < source_sentence_.size(); s++) {

      if ((s% 10000) == 0)
	std::cerr << "sentence pair #" << s << std::endl;

      const Storage1D<uint>& cur_source = source_sentence_[s];
      const Storage1D<uint>& cur_target = target_sentence_[s];
      const SingleLookupTable& cur_lookup = get_wordlookup(cur_source,cur_target,wcooc_,
                                                           nSourceWords_,slookup_[s],aux_lookup);
      
      const uint curI = cur_target.size();
      const uint curJ = cur_source.size();

      //std::cerr << "curJ: " << curJ << ", curI: " << curI << std::endl;
      
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

	if (hillclimb_mode_ == HillclimbingRestart) 
	  initial_alignment[s] = best_known_alignment_[s]; //since before nothing useful was set
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

      //std::cerr << "sentence_prob: " << sentence_prob << std::endl;
      //std::cerr << "best prob: " << best_prob << std::endl;
      //std::cerr << "expansion prob: " << expansion_prob << std::endl;
      //std::cerr << "swap prob: " << swap_prob << std::endl;

      approx_sum_perplexity -= std::log(sentence_prob);
      
      const long double inv_sentence_prob = 1.0 / sentence_prob;

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
      //std::cerr << "update of distortion counts" << std::endl;
      
      NamedStorage1D<std::vector<int> > aligned_source_words(curI+1,MAKENAME(aligned_source_words));
      for (uint j=0; j < curJ; j++) {
        const uint cur_aj = best_known_alignment_[s][j];
        aligned_source_words[cur_aj].push_back(j);	
      }

      //std::cerr << "a) viterbi" << std::endl;

      // 1. handle viterbi alignment
      int cur_prev_cept = -1;
      int prev_cept_center = -1;
      for (uint i=1; i <= curI; i++) {
	
	uint tclass = target_class_[ cur_target[i-1] ];
	
	const long double cur_prob = inv_sentence_prob * best_prob;
	
	if (fertility[i] > 0) {
	  
	  const std::vector<int>& cur_aligned_source_words = aligned_source_words[i];
	    
	  const uint first_j = cur_aligned_source_words[0];
	  
	  //a) update head prob
	  if (cur_prev_cept >= 0) {
	    
	    const uint sclass = source_class_[ cur_source[first_j]];
	    
	    if (inter_dist_mode_ == IBM4InterDistModePrevious)
	      tclass = target_class_[cur_target[cur_prev_cept-1] ];
	    
	    int diff = first_j - prev_cept_center;
	    diff += displacement_offset_;
	    
	    fceptstart_count(sclass,tclass,diff) += cur_prob;
	    
	    if (!nondeficient_ && reduce_deficiency_) {
	      if (inter_distort_count[curJ].size() == 0 || inter_distort_count[curJ](sclass,tclass).size() == 0)
		sparse_inter_distort_count(sclass,tclass)[DistortCount(curJ,first_j,prev_cept_center)] += cur_prob;
	      else
		inter_distort_count[curJ](sclass,tclass)(first_j,prev_cept_center) += cur_prob;
	    }
	  }
	  else if (use_sentence_start_prob_) {
	    sentence_start_count[curJ][first_j] += cur_prob;
	  }
	  
	  //b) update within-cept prob
	  int prev_aligned_j = first_j;
	  
	  for (uint k=1; k < cur_aligned_source_words.size(); k++) {
	      
	    const int cur_j = cur_aligned_source_words[k]; 
	    
	    const uint cur_class = (intra_dist_mode_ == IBM4IntraDistModeSource) ? source_class_[cur_source[cur_j]]
	      : target_class_[cur_target[i-1]];
	    
	    int diff = cur_j - prev_aligned_j;
	    fwithincept_count(cur_class,diff) += cur_prob;
	    
	    if (reduce_deficiency_)
	      intra_distort_count[curJ](cur_class,cur_j,prev_aligned_j) += cur_prob;
	    
	    prev_aligned_j = cur_j;
	  }
	  
	  
	  switch (cept_start_mode_) {
	  case IBM4CENTER: {
	    double sum = 0.0;
	    for (uint k=0; k < cur_aligned_source_words.size(); k++)
	      sum += cur_aligned_source_words[k];
	    prev_cept_center = (int) round(sum / fertility[i]);
	    break;
	  }
	  case IBM4FIRST:
	    prev_cept_center = cur_aligned_source_words[0]; 
	    break;
	  case IBM4LAST: {
	    prev_cept_center = cur_aligned_source_words.back();
	    break;
	  }
	  case IBM4UNIFORM:
	    prev_cept_center = cur_aligned_source_words[0];
	    break;
	  default:
	    assert(false);
	  }

	  cur_prev_cept = i;
	}
      }
	
      //std::cerr << "b) expansion" << std::endl;

      // 2. handle expansion moves
      NamedStorage1D<std::vector<int> > exp_aligned_source_words(MAKENAME(exp_aligned_source_words));
      exp_aligned_source_words = aligned_source_words;
      
      for (uint exp_j=0; exp_j < curJ; exp_j++) {
	
	const uint cur_aj = best_known_alignment_[s][exp_j];
	
	exp_aligned_source_words[cur_aj].erase(std::find(exp_aligned_source_words[cur_aj].begin(),
							 exp_aligned_source_words[cur_aj].end(),exp_j));
	
	for (uint exp_i=0; exp_i <= curI; exp_i++) {
	  
	  long double cur_prob = expansion_move_prob(exp_j,exp_i);
	  
	  if (cur_prob > best_prob * 1e-11) {
	    
	    cur_prob *= inv_sentence_prob;
	    
	    //modify
	    exp_aligned_source_words[exp_i].push_back(exp_j);
	    vec_sort(exp_aligned_source_words[exp_i]);
	    
	    
	    int prev_center = -100;
	    int prev_cept = -1;
	    
	    for (uint i=1; i <= curI; i++) {
	      
	      const std::vector<int>& cur_aligned_source_words = exp_aligned_source_words[i];
	      
	      if (cur_aligned_source_words.size() > 0) {
		
		uint tclass = target_class_[cur_target[i-1]];
		const int first_j = cur_aligned_source_words[0];
		
		//collect counts for the head model
		if (prev_center >= 0) {
		  
		  const uint sclass = source_class_[ cur_source[first_j] ];
		  
		  if (inter_dist_mode_ == IBM4InterDistModePrevious)
		    tclass = target_class_[cur_target[prev_cept-1] ];
		  
		  int diff =  first_j - prev_center;
		  diff += displacement_offset_;
		  fceptstart_count(sclass,tclass,diff) += cur_prob;
		  
		  if (!nondeficient_ && reduce_deficiency_) {
		    if (inter_distort_count[curJ].size() == 0 || inter_distort_count[curJ](sclass,tclass).size() == 0)
		      sparse_inter_distort_count(sclass,tclass)[DistortCount(curJ,first_j,prev_center)] += cur_prob;
		    else
		      inter_distort_count[curJ](sclass,tclass)(first_j,prev_center) += cur_prob;
		  }
		}
		else if (use_sentence_start_prob_) {
		  sentence_start_count[curJ][first_j] += cur_prob;
		}
		
		//collect counts for the within-cept model
		int prev_j = first_j;
		
		for (uint k=1; k < cur_aligned_source_words.size(); k++) {
		  
		  const int cur_j = cur_aligned_source_words[k];
		  
		  const uint cur_class = (intra_dist_mode_ == IBM4IntraDistModeSource) ? source_class_[cur_source[cur_j]]
		    : target_class_[cur_target[i-1]];
		  
		  int diff = cur_j - prev_j;
		  fwithincept_count(cur_class,diff) += cur_prob;
		  
		  if (reduce_deficiency_)
		    intra_distort_count[curJ](cur_class,cur_j,prev_j) += cur_prob;
		  
		  prev_j = cur_j;
		}
		
		//update prev_center
		switch (cept_start_mode_) {
		case IBM4CENTER: {
		  
		  double sum_j = 0;
		  uint nAlignedWords = cur_aligned_source_words.size();
		  for (uint k=0; k < nAlignedWords; k++)
		    sum_j += cur_aligned_source_words[k];
		  
		  prev_center = (int) round(sum_j / nAlignedWords);
		  break;
		}
		case IBM4FIRST:
		  prev_center = first_j;
		  break;
		case IBM4LAST: {
		  prev_center = prev_j; //prev_j was set to the last pos in the above loop
		  break;
		}
		case IBM4UNIFORM:
		  prev_center = first_j; //will not be used
		  break;
		default:
		  assert(false);
		}
		
		prev_cept = i;
	      }
	    }
	    
	    //restore
	    exp_aligned_source_words[exp_i] = aligned_source_words[exp_i];
	  }
	}
	
	exp_aligned_source_words[cur_aj] = aligned_source_words[cur_aj];
      }
      
      //std::cerr << "c) swap" << std::endl;

      //3. handle swap moves
      NamedStorage1D<std::vector<int> > swap_aligned_source_words(MAKENAME(swap_aligned_source_words));
      swap_aligned_source_words = aligned_source_words;
      
      for (uint swap_j1 = 0; swap_j1 < curJ; swap_j1++) {
	
	const uint aj1 = best_known_alignment_[s][swap_j1];
	
	for (uint swap_j2 = 0; swap_j2 < curJ; swap_j2++) {
	  
	  long double cur_prob = swap_move_prob(swap_j1, swap_j2);
	  
	  if (cur_prob > best_prob * 1e-11) {
	    
	    cur_prob *= inv_sentence_prob;
	    
	    const uint aj2 = best_known_alignment_[s][swap_j2];
	    
	    //modify
	    std::vector<int>::iterator it = std::find(swap_aligned_source_words[aj1].begin(),
						      swap_aligned_source_words[aj1].end(),swap_j1);
	    swap_aligned_source_words[aj1].erase(it);
	    it = std::find(swap_aligned_source_words[aj2].begin(),swap_aligned_source_words[aj2].end(),swap_j2);
	    swap_aligned_source_words[aj2].erase(it);
	    swap_aligned_source_words[aj1].push_back(swap_j2);
	    swap_aligned_source_words[aj2].push_back(swap_j1);
	    
	    vec_sort(swap_aligned_source_words[aj1]);
	    vec_sort(swap_aligned_source_words[aj2]);
	    
	    int prev_center = -100;
	    int prev_cept = -1;
	    
	    for (uint i=1; i <= curI; i++) {
	      
	      const std::vector<int>& cur_aligned_source_words = swap_aligned_source_words[i];
	      
	      if (cur_aligned_source_words.size() > 0) {
		
		uint tclass = target_class_[cur_target[i-1]];
		
		double sum_j = 0;
		uint nAlignedWords = 0;
		
		const int first_j = cur_aligned_source_words[0];
		
		sum_j = first_j;
		nAlignedWords++;
		
		//collect counts for the head model
		if (prev_center >= 0) {
		    
		  const uint sclass = source_class_[ cur_source[first_j] ];
		  
		  if (inter_dist_mode_ == IBM4InterDistModePrevious)		
		    tclass = target_class_[cur_target[prev_cept-1] ];
		  
		  int diff =  first_j - prev_center;
		  diff += displacement_offset_;
		  fceptstart_count(sclass,tclass,diff) += cur_prob;
		  
		  if (!nondeficient_ && reduce_deficiency_) {
		    if (inter_distort_count[curJ].size() == 0 || inter_distort_count[curJ](sclass,tclass).size() == 0)
		      sparse_inter_distort_count(sclass,tclass)[DistortCount(curJ,first_j,prev_center)] += cur_prob;
		    else
		      inter_distort_count[curJ](sclass,tclass)(first_j,prev_center) += cur_prob;
		  }
		}
		else if (use_sentence_start_prob_) {
		  sentence_start_count[curJ][first_j] += cur_prob;
		}
		
		//collect counts for the within-cept model
		int prev_j = first_j;
		
		for (uint k=1; k < cur_aligned_source_words.size(); k++) {
		  
		  const int cur_j = cur_aligned_source_words[k];
		  sum_j += cur_j;
		  nAlignedWords++;
		  
		  const uint cur_class = (intra_dist_mode_ == IBM4IntraDistModeSource) ? source_class_[cur_source[cur_j]]
		    : target_class_[cur_target[i-1]];
		  
		  int diff = cur_j - prev_j;
		  fwithincept_count(cur_class,diff) += cur_prob;
		  
		  if (reduce_deficiency_)
		    intra_distort_count[curJ](cur_class,cur_j,prev_j) += cur_prob;
		  
		  prev_j = cur_j;
		}
		
		//update prev_center
		switch (cept_start_mode_) {
		case IBM4CENTER:
		  prev_center = (int) round(sum_j / nAlignedWords);
		  break;
		case IBM4FIRST:
		  prev_center = first_j;
		  break;
		case IBM4LAST: {
		  prev_center = prev_j; //prev_j was set to the last pos in the above loop
		  break;
		}
		case IBM4UNIFORM:
		  prev_center = first_j; //will not be used
		  break;
		default:
		  assert(false);
		}
		
		prev_cept = i;
	      }
	    }
	    
	    //restore
	    swap_aligned_source_words[aj1] = aligned_source_words[aj1];
	    swap_aligned_source_words[aj2] = aligned_source_words[aj2];
	  }
	}
      }

      if (nondeficient_) {

        //a) best known alignment (=mode)
        const double mode_contrib = inv_sentence_prob * best_prob;

        int prev_cept_center = -1;
	int prev_cept = -1;

        Storage1D<bool> fixed(curJ,false);
     
        for (uint i=1; i <= curI; i++) {
          
          if (aligned_source_words[i].size() > 0) {

            const uint ti = cur_target[i-1];
            uint tclass = target_class_[ti];
            
            const int first_j = aligned_source_words[i][0];

            uint nToRemove = aligned_source_words[i].size()-1;

            //handle the head of the cept
            if (prev_cept_center != -1 && cept_start_mode_ != IBM4UNIFORM) {
              
	      const uint sclass = source_class_[cur_source[first_j]];

	      if (inter_dist_mode_ == IBM4InterDistModePrevious)
		tclass = target_class_[cur_target[prev_cept-1]];              

              std::vector<uchar> possible_diffs;
              
              for (int j=0; j < int(curJ); j++) {
                if (!fixed[j]) {
                  possible_diffs.push_back(j-prev_cept_center+displacement_offset_);
                }
              }
              
              if (nToRemove > 0) {
                possible_diffs.resize(possible_diffs.size()-nToRemove);
              }

              if (possible_diffs.size() > 1) { //no use storing cases where only one pos. is available
              
                Math1D::Vector<uchar,uchar> vec_possible_diffs(possible_diffs.size());
	        assign(vec_possible_diffs,possible_diffs);

                nondef_cept_start_count(sclass,tclass)[vec_possible_diffs] += mode_contrib;

		fnondef_ceptstart_singleton_count(sclass,tclass,first_j-prev_cept_center+displacement_offset_) += mode_contrib;
              }
            }
            fixed[first_j] = true;

            //handle the body of the cept
            int prev_j = first_j;
            for (uint k=1; k < aligned_source_words[i].size(); k++) {
              
              nToRemove--;
              
              std::vector<uchar> possible_diffs;

              const int cur_j = aligned_source_words[i][k];

	      const uint cur_class = (intra_dist_mode_ == IBM4IntraDistModeSource) ? source_class_[cur_source[cur_j]]
		: target_class_[cur_target[i-1]];

              for (int j=prev_j+1; j < int(curJ); j++) {
                possible_diffs.push_back(j-prev_j);
              }

              if (nToRemove > 0) {
                possible_diffs.resize(possible_diffs.size()-nToRemove);
              }

              if (possible_diffs.size() > 1) { //no use storing cases where only one pos. is available

                Math1D::Vector<uchar,uchar> vec_possible_diffs(possible_diffs.size());
		assign(vec_possible_diffs,possible_diffs);

                nondef_within_cept_count[cur_class][vec_possible_diffs] += mode_contrib;

		fnondef_withincept_singleton_count(cur_class,cur_j-prev_j) += mode_contrib;
	      }
              
              fixed[cur_j] = true;
              
              prev_j = cur_j;
            }
            
            switch (cept_start_mode_) {
            case IBM4CENTER : {
              
              //compute the center of this cept and store the result in prev_cept_center
              double sum = 0.0;
              for (uint k=0; k < aligned_source_words[i].size(); k++) {
                sum += aligned_source_words[i][k];
              }
              
              prev_cept_center = (int) round(sum / aligned_source_words[i].size());
              break;
            }
            case IBM4FIRST:
              prev_cept_center = first_j;
              break;
            case IBM4LAST: {
              prev_cept_center = prev_j; //was set to the last pos in the above llop
              break;
            }
            case IBM4UNIFORM:
              prev_cept_center = first_j; //will not be used
              break;
            default:
              assert(false);
            }
            
	    prev_cept = i;
            assert(prev_cept_center >= 0);
          }
        }
        
        
        Storage1D<std::vector<int> > hyp_aligned_source_words = aligned_source_words;

        //b) expansion moves
        for (uint jj=0; jj < curJ; jj++) {

          uint cur_aj = best_known_alignment_[s][jj];
          
          for (uint aj = 0; aj <= curI; aj++) {

            const double contrib = expansion_move_prob(jj,aj) / sentence_prob;
 
            if (contrib > nondef_thresh) {
              
              assert(aj != cur_aj);

              hyp_aligned_source_words[cur_aj].erase(std::find(hyp_aligned_source_words[cur_aj].begin(),
                                                               hyp_aligned_source_words[cur_aj].end(),jj));
              hyp_aligned_source_words[aj].push_back(jj);
	      vec_sort(hyp_aligned_source_words[aj]);


              int prev_cept_center = -1;
	      int prev_cept = -1;

              Storage1D<bool> fixed(curJ,false);
              
              for (uint i=1; i <= curI; i++) {
                
                if (hyp_aligned_source_words[i].size() > 0) {

                  const uint ti = cur_target[i-1];
                  uint tclass = target_class_[ti];
                  
                  const int first_j = hyp_aligned_source_words[i][0];

                  uint nToRemove = hyp_aligned_source_words[i].size()-1;

                  //handle the head of the cept
                  if (prev_cept_center != -1) {
                    
                    if (cept_start_mode_ != IBM4UNIFORM) {
                      
		      const uint sclass = source_class_[cur_source[first_j]];

		      if (inter_dist_mode_ == IBM4InterDistModePrevious)		  
			tclass = target_class_[cur_target[prev_cept-1]];
                      
                      std::vector<uchar> possible_diffs;
                      
                      for (int j=0; j < int(curJ); j++) {
                        if (!fixed[j]) {
                          possible_diffs.push_back(j-prev_cept_center+displacement_offset_);
                        }
                      }
                      
                      if (nToRemove > 0) {
                        possible_diffs.resize(possible_diffs.size()-nToRemove);
                      }
                      
                      if (possible_diffs.size() > 1) { //no use storing cases where only one pos. is available

                        Math1D::Vector<uchar,uchar> vec_possible_diffs(possible_diffs.size());
			assign(vec_possible_diffs,possible_diffs);
                        
                        nondef_cept_start_count(sclass,tclass)[vec_possible_diffs] += contrib;

			fnondef_ceptstart_singleton_count(sclass,tclass,first_j-prev_cept_center+displacement_offset_) += contrib;
		      }
                    }
                  }
                  fixed[first_j] = true;
                  
                  //handle the body of the cept
                  int prev_j = first_j;
                  for (uint k=1; k < hyp_aligned_source_words[i].size(); k++) {
              
                    nToRemove--;
              
                    std::vector<uchar> possible_diffs;
                    
                    const int cur_j = hyp_aligned_source_words[i][k];

		    const uint cur_class = (intra_dist_mode_ == IBM4IntraDistModeSource) ? source_class_[cur_source[cur_j]]
		      : target_class_[cur_target[i-1]];

                    for (int j=prev_j+1; j < int(curJ); j++) {
                      possible_diffs.push_back(j-prev_j);
                    }
                    
                    if (nToRemove > 0) {
                      possible_diffs.resize(possible_diffs.size()-nToRemove);
                    }
                    
                    if (possible_diffs.size() > 1) { //no use storing cases where only one pos. is available

                      Math1D::Vector<uchar,uchar> vec_possible_diffs(possible_diffs.size());
		      assign(vec_possible_diffs,possible_diffs);
                    
		      fnondef_withincept_singleton_count(cur_class,cur_j-prev_j) += contrib;

                      nondef_within_cept_count[cur_class][vec_possible_diffs] += contrib;
		    }
              
                    fixed[cur_j] = true;
                    
                    prev_j = cur_j;
                  }
                  
                  switch (cept_start_mode_) {
                  case IBM4CENTER : {
                    
                    //compute the center of this cept and store the result in prev_cept_center
                    double sum = 0.0;
                    for (uint k=0; k < hyp_aligned_source_words[i].size(); k++) {
                      sum += hyp_aligned_source_words[i][k];
                    }
              
                    prev_cept_center = (int) round(sum / hyp_aligned_source_words[i].size());
                    break;
                  }
                  case IBM4FIRST:
                    prev_cept_center = first_j;
                    break;
                  case IBM4LAST:
                    prev_cept_center = prev_j; //was set to the last pos in the above loop
                    break;
                  case IBM4UNIFORM:
                    prev_cept_center = first_j;
                    break;
                  default:
                    assert(false);
                  }

		  prev_cept = i;
                  assert(prev_cept_center >= 0);
                }
              }

              hyp_aligned_source_words[cur_aj] = aligned_source_words[cur_aj];
              hyp_aligned_source_words[aj] = aligned_source_words[aj];
            }
          }
        }


        //c) swap moves
        for (uint j1=0; j1 < curJ-1; j1++) {

          uint aj1 = best_known_alignment_[s][j1];

          for (uint j2=j1+1; j2 < curJ; j2++) {

            const double contrib = swap_move_prob(j1,j2) / sentence_prob;

            if (contrib > nondef_thresh) {

              uint aj2 = best_known_alignment_[s][j2];

              hyp_aligned_source_words[aj1].erase(std::find(hyp_aligned_source_words[aj1].begin(),
                                                            hyp_aligned_source_words[aj1].end(),j1));
              hyp_aligned_source_words[aj1].push_back(j2);
	      vec_sort(hyp_aligned_source_words[aj1]);
              hyp_aligned_source_words[aj2].erase(std::find(hyp_aligned_source_words[aj2].begin(),
                                                            hyp_aligned_source_words[aj2].end(),j2));
              hyp_aligned_source_words[aj2].push_back(j1);
	      vec_sort(hyp_aligned_source_words[aj2]);
              
              int prev_cept_center = -1;
	      int prev_cept = -1;

              Storage1D<bool> fixed(curJ,false);
              
              for (uint i=1; i <= curI; i++) {
                
                if (hyp_aligned_source_words[i].size() > 0) {

                  const uint ti = cur_target[i-1];
                  uint tclass = target_class_[ti];
                  
                  const int first_j = hyp_aligned_source_words[i][0];

                  uint nToRemove = hyp_aligned_source_words[i].size()-1;

                  //handle the head of the cept
                  if (prev_cept_center != -1) {
                    
                    if (cept_start_mode_ != IBM4UNIFORM) {
                      
		      const uint sclass = source_class_[cur_source[first_j]];

		      if (inter_dist_mode_ == IBM4InterDistModePrevious)		  		   
			tclass = target_class_[cur_target[prev_cept-1]];

                      std::vector<uchar> possible_diffs;
                      
                      for (int j=0; j < int(curJ); j++) {
                        if (!fixed[j]) {
                          possible_diffs.push_back(j-prev_cept_center+displacement_offset_);
                        }
                      }
                      
                      if (nToRemove > 0) {
                        possible_diffs.resize(possible_diffs.size()-nToRemove);
                      }
                     
                      if (possible_diffs.size() > 1) { //no use storing cases where only one pos. is available

                        Math1D::Vector<uchar,uchar> vec_possible_diffs(possible_diffs.size());
			assign(vec_possible_diffs,possible_diffs);
                      
                        nondef_cept_start_count(sclass,tclass)[vec_possible_diffs] += contrib;

			fnondef_ceptstart_singleton_count(sclass,tclass,first_j - prev_cept_center+displacement_offset_) += contrib;
		      }
                    }
                  }
                  fixed[first_j] = true;
                  
                  //handle the body of the cept
                  int prev_j = first_j;
                  for (uint k=1; k < hyp_aligned_source_words[i].size(); k++) {
              
                    nToRemove--;
              
                    std::vector<uchar> possible_diffs;

                    const int cur_j = hyp_aligned_source_words[i][k];
                    
		    const uint cur_class = (intra_dist_mode_ == IBM4IntraDistModeSource) ? source_class_[cur_source[cur_j]]
		      : target_class_[cur_target[i-1]];

                    for (int j=prev_j+1; j < int(curJ); j++) {
                      possible_diffs.push_back(j-prev_j);
                    }
                    
                    if (nToRemove > 0) {
                      possible_diffs.resize(possible_diffs.size()-nToRemove);
                    }
                    
                    if (possible_diffs.size() > 1) { //no use storing cases where only one pos. is available

                      Math1D::Vector<uchar,uchar> vec_possible_diffs(possible_diffs.size());
		      assign(vec_possible_diffs,possible_diffs);
                      
                      nondef_within_cept_count[cur_class][vec_possible_diffs] += contrib;

		      fnondef_withincept_singleton_count(cur_class,cur_j-prev_j) += contrib;
                    }
              
                    fixed[cur_j] = true;
                    
                    prev_j = cur_j;
                  }
                  
                  switch (cept_start_mode_) {
                  case IBM4CENTER : {
                    
                    //compute the center of this cept and store the result in prev_cept_center
                    double sum = 0.0;
                    for (uint k=0; k < hyp_aligned_source_words[i].size(); k++) {
                      sum += hyp_aligned_source_words[i][k];
                    }
              
                    prev_cept_center = (int) round(sum / hyp_aligned_source_words[i].size());
                    break;
                  }
                  case IBM4FIRST:
                    prev_cept_center = first_j;
                    break;
                  case IBM4LAST:
                    prev_cept_center = prev_j; //was set to the last pos in the above loop
                    break;
                  case IBM4UNIFORM:
                    prev_cept_center = first_j;
                    break;
                  default:
                    assert(false);
                  }

		  prev_cept = i;
                  assert(prev_cept_center >= 0);
                }
              }


              hyp_aligned_source_words[aj1] = aligned_source_words[aj1];
              hyp_aligned_source_words[aj2] = aligned_source_words[aj2];
            }
          }
        }
      } //end -- if (nondeficient_)
      
      tCountCollectEnd = std::clock();
      countcollecttime += diff_seconds(tCountCollectEnd,tCountCollectStart);


      //clean up cache
      for (uint j=0; j < inter_distortion_cache_[curJ].size(); j++)
        inter_distortion_cache_[curJ][j].clear();

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



    //update distortion probabilities

    //a) inter distortion
    if (nondeficient_) {

      IBM4CeptStartModel hyp_cept_start_prob = cept_start_prob_;

      for (uint x=0; x < cept_start_prob_.xDim(); x++) {
        std::cerr << "nondeficient inter-m-step(" << x << ",*)" << std::endl;
        for (uint y=0; y < cept_start_prob_.yDim(); y++) {

          std::map<Math1D::Vector<uchar,uchar>,double>& cur_map = nondef_cept_start_count(x,y);

	  std::vector<Math1D::Vector<uchar,uchar> > open_pos(cur_map.size());
	  std::vector<double> weight(cur_map.size());

          uint k=0;
          for (std::map<Math1D::Vector<uchar,uchar>,double>::const_iterator it = cur_map.begin(); it != cur_map.end(); it++) {
	    open_pos[k] = it->first;
	    weight[k] = it->second;

            k++;
          }


          cur_map.clear();

          double cur_energy = nondeficient_inter_m_step_energy(fnondef_ceptstart_singleton_count,open_pos,weight,cept_start_prob_,x,y);

          double sum = 0.0;
          for (uint k=0; k < cept_start_prob_.zDim(); k++) {
            sum += fceptstart_count(x,y,k);
          }

          if (sum > 1e-300) {

            for (uint k=0; k < cept_start_prob_.zDim(); k++) {
              hyp_cept_start_prob(x,y,k) = std::max(1e-15,fceptstart_count(x,y,k) / sum);
            }

            const double hyp_energy = nondeficient_inter_m_step_energy(fnondef_ceptstart_singleton_count,open_pos,weight,
								       hyp_cept_start_prob,x,y);

            if (hyp_energy < cur_energy) {
	      for (uint k=0; k < cept_start_prob_.zDim(); k++)
		cept_start_prob_(x,y,k) = hyp_cept_start_prob(x,y,k);
              cur_energy = hyp_energy;
            }
          }
          
          nondeficient_inter_m_step_with_interpolation(fnondef_ceptstart_singleton_count,open_pos,weight,x,y,cur_energy);
        }
      }
    }
    else {

      IBM4CeptStartModel hyp_cept_start_prob;
      if (reduce_deficiency_)
	hyp_cept_start_prob = cept_start_prob_;

      for (uint x=0; x < cept_start_prob_.xDim(); x++) {
        std::cerr << "inter-m-step(" << x << ",*)" << std::endl;

        for (uint y=0; y < cept_start_prob_.yDim(); y++) {
          
          double sum = 0.0;
          for (uint d=0; d < cept_start_prob_.zDim(); d++) 
            sum += fceptstart_count(x,y,d);
          
          if (sum > 1e-305) {

            const double inv_sum = 1.0 / sum;

            if (!reduce_deficiency_) {
              for (uint d=0; d < cept_start_prob_.zDim(); d++) 
                cept_start_prob_(x,y,d) = std::max(1e-8,inv_sum * fceptstart_count(x,y,d));
            }
            else {
              
              for (uint d=0; d < cept_start_prob_.zDim(); d++) 
                hyp_cept_start_prob(x,y,d) = std::max(1e-15,inv_sum * fceptstart_count(x,y,d));
              
              double cur_energy = inter_distortion_m_step_energy(inter_distort_count,sparse_inter_distort_count(x,y),
                                                                 cept_start_prob_,x,y);
              double hyp_energy = inter_distortion_m_step_energy(inter_distort_count,sparse_inter_distort_count(x,y),
                                                                 hyp_cept_start_prob,x,y);
              
              if (hyp_energy < cur_energy) {
		for (uint d=0; d < cept_start_prob_.zDim(); d++) 
		  cept_start_prob_(x,y,d) = hyp_cept_start_prob(x,y,d);

		cur_energy = hyp_energy;
	      }
            }
          }

          if (reduce_deficiency_) 
            inter_distortion_m_step(inter_distort_count,sparse_inter_distort_count(x,y),x,y);
        }
      }

      par2nonpar_inter_distortion();
    }


    //b) within-cept
    if (nondeficient_) {

      IBM4WithinCeptModel hyp_withincept_prob = within_cept_prob_;

      for (uint x=0; x < within_cept_prob_.xDim(); x++) {
        std::cerr << "calling nondeficient intra-m-step(" << x << ")" << std::endl;

        std::map<Math1D::Vector<uchar,uchar>,double>& cur_map = nondef_within_cept_count[x];

        std::vector<std::pair<Math1D::Vector<uchar,uchar>,double> > count(cur_map.size());

        uint k=0;
        for (std::map<Math1D::Vector<uchar,uchar>,double>::const_iterator it = cur_map.begin(); it != cur_map.end(); it++) {
          std::pair<Math1D::Vector<uchar,uchar>,double> new_pair;

          count[k] = *it;
          k++;
        }

        cur_map.clear();

        double sum = 0.0;
        for (uint d=0; d < within_cept_prob_.yDim(); d++)
          sum += fwithincept_count(x,d);
	
        if (sum > 1e-305) {
            
          const double inv_sum = 1.0 / sum;
          for (uint d=0; d < within_cept_prob_.yDim(); d++)
            hyp_withincept_prob(x,d) = std::max(1e-15,inv_sum * fwithincept_count(x,d));

          double cur_energy = nondeficient_intra_m_step_energy(fnondef_withincept_singleton_count,count,within_cept_prob_,x);
          double hyp_energy = nondeficient_intra_m_step_energy(fnondef_withincept_singleton_count,count,hyp_withincept_prob,x);

          if (hyp_energy < cur_energy) {
	    for (uint d=0; d < within_cept_prob_.yDim(); d++)
	      within_cept_prob_(x,d) = hyp_withincept_prob(x,d);
	  }
        }
        
        nondeficient_intra_m_step(fnondef_withincept_singleton_count,count,x);
      }
    }
    else {

      IBM4WithinCeptModel hyp_withincept_prob;
      if (reduce_deficiency_)
	hyp_withincept_prob = within_cept_prob_;

      for (uint x=0; x < within_cept_prob_.xDim(); x++) {
      
        double sum = 0.0;
        for (uint d=0; d < within_cept_prob_.yDim(); d++)
          sum += fwithincept_count(x,d);
	
        if (sum > 1e-305) {

          const double inv_sum = 1.0 / sum;
            
          if (!reduce_deficiency_) {
            for (uint d=0; d < within_cept_prob_.yDim(); d++)
              within_cept_prob_(x,d) = std::max(inv_sum * fwithincept_count(x,d),1e-8);
          }
          else {
            for (uint d=0; d < within_cept_prob_.yDim(); d++)
              hyp_withincept_prob(x,d) = std::max(1e-15,inv_sum * fwithincept_count(x,d));
            
            double cur_energy = intra_distortion_m_step_energy(intra_distort_count,within_cept_prob_,x);
            double hyp_energy = intra_distortion_m_step_energy(intra_distort_count,hyp_withincept_prob,x);
            
            if (hyp_energy < cur_energy) {
	      for (uint d=0; d < within_cept_prob_.yDim(); d++)
		within_cept_prob_(x,d) = hyp_withincept_prob(x,d); 
	    }
          }
        }
        
        std::cerr << "intra-m-step(" << x << ")" << std::endl;
        if (reduce_deficiency_)
          intra_distortion_m_step(intra_distort_count,x);
      }

      par2nonpar_intra_distortion();
    }

    //c) sentence start prob
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

    std::cerr << "IBM-4 max-perplex-energy in between iterations #" << (iter-1) << " and " << iter << transfer << ": "
              << max_perplexity << std::endl;
    std::cerr << "IBM-4 approx-sum-perplex-energy in between iterations #" << (iter-1) << " and " << iter << transfer << ": "
              << approx_sum_perplexity << std::endl;
    
    if (possible_ref_alignments_.size() > 0) {
      
      std::cerr << "#### IBM-4-AER in between iterations #" << (iter-1) << " and " << iter << transfer 
		<< ": " << AER() << std::endl;
      std::cerr << "#### IBM-4-fmeasure in between iterations #" << (iter-1) << " and " << iter << transfer 
		<< ": " << f_measure() << std::endl;
      std::cerr << "#### IBM-4-DAE/S in between iterations #" << (iter-1) << " and " << iter << transfer << ": " 
                << DAE_S() << std::endl;
    }

    std::cerr << (((double) sum_iter) / source_sentence_.size()) << " average hillclimbing iterations per sentence pair" 
              << std::endl;     
  }

  std::cerr << "spent " << hillclimbtime << " seconds on IBM-4-hillclimbing" << std::endl;
  std::cerr << "spent " << countcollecttime << " seconds on IBM-4-distortion count collection" << std::endl;

  iter_offs_ = iter-1;
}

void IBM4Trainer::train_viterbi(uint nIter, FertilityModelTrainer* fert_trainer, HmmWrapper* wrapper) {

  const uint nSentences = source_sentence_.size();

  std::cerr << "starting IBM-4 Viterbi training without constraints";
  if (fert_trainer != 0)
    std::cerr << " (init from " << fert_trainer->model_name() <<  ") "; 
  else if (wrapper != 0)
    std::cerr << " (init from HMM) ";
  std::cerr << std::endl;

  Storage1D<Math1D::Vector<AlignBaseType> > initial_alignment;
  if (hillclimb_mode_ == HillclimbingRestart) 
    initial_alignment = best_known_alignment_;


  double max_perplexity = 0.0;

  IBM4CeptStartModel fceptstart_count(cept_start_prob_.xDim(),cept_start_prob_.yDim(),2*maxJ_-1,MAKENAME(fceptstart_count));
  IBM4WithinCeptModel fwithincept_count(within_cept_prob_.xDim(),within_cept_prob_.yDim(),MAKENAME(fwithincept_count));

  Storage1D<Storage2D<Math2D::Matrix<double> > > inter_distort_count(maxJ_+1);
  Storage1D<Math3D::Tensor<double> > intra_distort_count(maxJ_+1);

  Storage1D<Math1D::Vector<double> > sentence_start_count(maxJ_+1);


  if (log_table_.size() < nSentences) {
    EXIT("passed log table is not large enough.");
  }

  for (uint J=1; J <= maxJ_; J++) {

    if (reduce_deficiency_) {
      inter_distort_count[J].resize(inter_distortion_prob_[J].xDim(),inter_distortion_prob_[J].yDim());

      
      for (uint x=0; x < inter_distortion_prob_[J].xDim(); x++)
	for (uint y=0; y < inter_distortion_prob_[J].yDim(); y++)
	  inter_distort_count[J](x,y).resize(inter_distortion_prob_[J](x,y).xDim(),inter_distortion_prob_[J](x,y).yDim(),0.0);

      intra_distort_count[J].resize(intra_distortion_prob_[J].xDim(),intra_distortion_prob_[J].yDim(),
                                    intra_distortion_prob_[J].zDim(),0.0);
    }
    
    sentence_start_count[J].resize(J,0);
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

  uint iter;
  for (iter=1+iter_offs_; iter <= nIter+iter_offs_; iter++) {

    std::cerr << "******* IBM-4 Viterbi-iteration #" << iter << std::endl;

    uint sum_iter = 0;

    fzero_count = 0.0;
    fnonzero_count = 0.0;

    fceptstart_count.set_constant(0.0);
    fwithincept_count.set_constant(0.0);

    for (uint i=0; i < nTargetWords; i++) {
      fwcount[i].set_constant(0.0);
      ffert_count[i].set_constant(0.0);
    }

    for (uint J=1; J <= maxJ_; J++) {
      if (inter_distort_count[J].size() > 0) {
        for (uint y=0; y < inter_distortion_prob_[J].yDim(); y++)
          for (uint x=0; x < inter_distortion_prob_[J].xDim(); x++)
            inter_distort_count[J](x,y).set_constant(0.0);
      }
      intra_distort_count[J].set_constant(0.0);
      sentence_start_count[J].set_constant(0.0);
    }

    Storage2D<std::map<DistortCount,double> > sparse_inter_distort_count(nSourceClasses_,nTargetClasses_);

    Storage2D<std::map<Math1D::Vector<uchar,uchar>,uint> > nondef_cept_start_count(nSourceClasses_,nTargetClasses_); 
    Storage1D<std::map<Math1D::Vector<uchar,uchar>,uint> > nondef_within_cept_count(nTargetClasses_); 

    //this count is almost like fceptstart_count, but includes no terms where only one position is available
    IBM4CeptStartModel fnondef_ceptstart_singleton_count(cept_start_prob_.xDim(),cept_start_prob_.yDim(),2*maxJ_-1,0.0,
							 MAKENAME(fnondef_ceptstart_singleton_count));

    //same for this count and fnondef_withincept_count
    IBM4WithinCeptModel fnondef_withincept_singleton_count(within_cept_prob_.xDim(),within_cept_prob_.yDim(),0.0,
							   MAKENAME(fnondef_withincept_singleton_count));


    SingleLookupTable aux_lookup;

    max_perplexity = 0.0;

    for (size_t s=0; s < nSentences; s++) {

      //DEBUG
      uint prev_sum_iter = sum_iter;
      //END_DEBUG

      if ((s% 10000) == 0)
        std::cerr << "sentence pair #" << s << std::endl;

      const Storage1D<uint>& cur_source = source_sentence_[s];
      const Storage1D<uint>& cur_target = target_sentence_[s];
      const SingleLookupTable& cur_lookup = get_wordlookup(cur_source,cur_target,wcooc_,
                                                           nSourceWords_,slookup_[s],aux_lookup);
      
      const uint curI = cur_target.size();
      const uint curJ = cur_source.size();
      
      Math1D::NamedVector<uint> fertility(curI+1,0,MAKENAME(fertility));

      //these will not actually be used, but need to be passed to the hillclimbing routine
      Math2D::NamedMatrix<long double> swap_move_prob(curJ,curJ,MAKENAME(swap_move_prob));
      Math2D::NamedMatrix<long double> expansion_move_prob(curJ,curI+1,MAKENAME(expansion_move_prob));

      //std::clock_t tHillclimbStart, tHillclimbEnd;
      //tHillclimbStart = std::clock();

      if (hillclimb_mode_ == HillclimbingRestart) 
	best_known_alignment_[s] = initial_alignment[s];


      long double best_prob = 0.0;

      if (fert_trainer != 0 && iter == 1) {

	best_prob = fert_trainer->update_alignment_by_hillclimbing(cur_source,cur_target,cur_lookup,sum_iter,fertility,
								   expansion_move_prob,swap_move_prob,best_known_alignment_[s]);	

	//DEBUG
#ifndef NDEBUG
	long double align_prob = alignment_prob(s,best_known_alignment_[s]);
      
	if (isinf(align_prob) || isnan(align_prob) || align_prob == 0.0) {
	  
	  std::cerr << "ERROR: after hillclimbing: align-prob for sentence " << s << " has prob " << align_prob << std::endl;
	  
	  print_alignment_prob_factors(source_sentence_[s], target_sentence_[s], slookup_[s], best_known_alignment_[s]);
	  
	  exit(1);
	}
#endif
	//END_DEBUG

      }
      else if (wrapper != 0) {

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

      //DEBUG
      if (isinf(max_perplexity)) {

	std::cerr << "ERROR: inf after sentence  " << s << ", last alignment prob: " << best_prob << std::endl;
	std::cerr << "J = " << curJ << ", I = " << curI << std::endl;
	std::cerr << "preceding number of hillclimbing iterations: " << (sum_iter - prev_sum_iter) << std::endl;

	exit(1);
      }
      //END_DEBUG

      //tHillclimbEnd = std::clock();


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
      NamedStorage1D<std::vector<AlignBaseType> > aligned_source_words(curI+1,MAKENAME(aligned_source_words));

      for (uint j=0; j < curJ; j++) {
        const uint cur_aj = best_known_alignment_[s][j];
        aligned_source_words[cur_aj].push_back(j);
      }


      // handle viterbi alignment
      int cur_prev_cept = -1;
      uint prev_cept_center = MAX_UINT;
      for (uint i=1; i <= curI; i++) {

        uint tclass = target_class_[ cur_target[i-1] ];

        if (fertility[i] > 0) {

	  const std::vector<AlignBaseType> cur_aligned_source_words = aligned_source_words[i];
	  
          //a) update head prob
          if (cur_prev_cept >= 0 && cept_start_mode_ != IBM4UNIFORM) {

	    const uint sclass = source_class_[cur_source[cur_aligned_source_words[0]]];

	    if (inter_dist_mode_ == IBM4InterDistModePrevious)		  		   
	      tclass = target_class_[cur_target[cur_prev_cept-1]];

            int diff = aligned_source_words[i][0] - prev_cept_center;
            diff += displacement_offset_;

            fceptstart_count(sclass,tclass,diff) += 1.0;

            if (!nondeficient_ && reduce_deficiency_) {
              if (inter_distort_count[curJ].size() == 0 || inter_distort_count[curJ](sclass,tclass).size() == 0)
                sparse_inter_distort_count(sclass,tclass)[DistortCount(curJ,cur_aligned_source_words[0],prev_cept_center)] += 1.0;
              else
                inter_distort_count[curJ](sclass,tclass)(cur_aligned_source_words[0],prev_cept_center) += 1.0;
            }
          }
          else if (use_sentence_start_prob_) {
            sentence_start_count[curJ][cur_aligned_source_words[0]] += 1.0;
          }

          //b) update within-cept prob
          int prev_aligned_j = aligned_source_words[i][0]; 

          for (uint k=1; k < aligned_source_words[i].size(); k++) {

            const int cur_j = aligned_source_words[i][k];

	    const uint cur_class = (intra_dist_mode_ == IBM4IntraDistModeSource) ? source_class_[cur_source[cur_j]]
	      : target_class_[cur_target[i-1]];

            int diff = cur_j - prev_aligned_j;
            fwithincept_count(cur_class,diff) += 1.0;

            if (reduce_deficiency_)
              intra_distort_count[curJ](cur_class,cur_j,prev_aligned_j) += 1.0;

            prev_aligned_j = cur_j;
          }


          switch (cept_start_mode_) {
          case IBM4CENTER: {
            double sum = 0.0;
            for (uint k=0; k < cur_aligned_source_words.size(); k++)
              sum += cur_aligned_source_words[k];

            prev_cept_center = (uint) round(sum / fertility[i]);
            break;
          }
          case IBM4FIRST:
            prev_cept_center = cur_aligned_source_words[0]; 
            break;
          case IBM4LAST:
            prev_cept_center = cur_aligned_source_words.back();
            break;
          case IBM4UNIFORM:
            prev_cept_center = cur_aligned_source_words[0]; //will not be used
            break;
          default:
            assert(false);
          }

	  cur_prev_cept = i;
        }
      }


      if (nondeficient_) {

        //a) best known alignment (=mode)
        int prev_cept_center = -1;
	int prev_cept = -1;

        Storage1D<bool> fixed(curJ,false);
     
        for (uint i=1; i <= curI; i++) {
          
          if (aligned_source_words[i].size() > 0) {

            const uint ti = cur_target[i-1];
            uint tclass = target_class_[ti];
            
            const int first_j = aligned_source_words[i][0];

            uint nToRemove = aligned_source_words[i].size()-1;

            //handle the head of the cept
            if (prev_cept_center != -1 && cept_start_mode_ != IBM4UNIFORM) {
              
	      const uint sclass = source_class_[cur_source[first_j]];

	      if (inter_dist_mode_ == IBM4InterDistModePrevious)
		tclass = target_class_[cur_target[prev_cept-1]];              

              std::vector<uchar> possible_diffs;
              
              for (int j=0; j < int(curJ); j++) {
                if (!fixed[j]) {
                  possible_diffs.push_back(j-prev_cept_center+displacement_offset_);
                }
              }
              
              if (nToRemove > 0) {
                possible_diffs.resize(possible_diffs.size()-nToRemove);
              }

              if (possible_diffs.size() > 1) { //no use storing cases where only one pos. is available

                Math1D::Vector<uchar,uchar> vec_possible_diffs(possible_diffs.size());
                for (uint k=0; k < possible_diffs.size(); k++)
                  vec_possible_diffs[k] = possible_diffs[k];

		fnondef_ceptstart_singleton_count(sclass,tclass,first_j-prev_cept_center+displacement_offset_) += 1.0;
                
                nondef_cept_start_count(sclass,tclass)[vec_possible_diffs] += 1.0;
              }
            }
            fixed[first_j] = true;

            //handle the body of the cept
            int prev_j = first_j;
            for (uint k=1; k < aligned_source_words[i].size(); k++) {
              
              nToRemove--;
              
              std::vector<uchar> possible;

              const int cur_j = aligned_source_words[i][k];

	      const uint cur_class = (intra_dist_mode_ == IBM4IntraDistModeSource) ? source_class_[cur_source[cur_j]]
		: target_class_[cur_target[i-1]];

              for (int j=prev_j+1; j < int(curJ); j++) {
                possible.push_back(j-prev_j);
              }

              if (nToRemove > 0) {
                possible.resize(possible.size()-nToRemove);
              }

              if (possible.size() > 1) { //no use storing cases where only one pos. is available

                Math1D::Vector<uchar,uchar> vec_possible(possible.size());
                for (uint k=0; k < possible.size(); k++)
                  vec_possible[k] = possible[k];

		fnondef_withincept_singleton_count(cur_class,cur_j-prev_j) += 1.0;
                nondef_within_cept_count[cur_class][vec_possible] += 1.0;
              }
              
              fixed[cur_j] = true;
              
              prev_j = cur_j;
            }
            
            switch (cept_start_mode_) {
            case IBM4CENTER : {
              
              //compute the center of this cept and store the result in prev_cept_center
              double sum = 0.0;
              for (uint k=0; k < aligned_source_words[i].size(); k++) {
                sum += aligned_source_words[i][k];
              }
              
              prev_cept_center = (int) round(sum / aligned_source_words[i].size());
              break;
            }
            case IBM4FIRST:
              prev_cept_center = first_j;
              break;
            case IBM4LAST: {
              prev_cept_center = prev_j; //was set to the last pos in the above llop
              break;
            }
            case IBM4UNIFORM:
              prev_cept_center = first_j; //will not be used
              break;
            default:
              assert(false);
            }
            
	    prev_cept = i;
            assert(prev_cept_center >= 0);
          }
        }
      }

      //clean up cache
      for (uint j=0; j < inter_distortion_cache_[curJ].size(); j++)
        inter_distortion_cache_[curJ][j].clear();

    } // loop over sentences finished

    /***** update probability models from counts *******/

    //update p_zero_ and p_nonzero_
    if (!fix_p0_) {
      double fsum = fzero_count + fnonzero_count;
      p_zero_ = std::max<double>(1e-15,fzero_count / fsum);
      p_nonzero_ = std::max<double>(1e-15,fnonzero_count / fsum);
    }

    std::cerr << "new p_zero: " << p_zero_ << std::endl;

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
    update_dict_from_counts(fwcount, prior_weight_, 0.0, iter,  false, 0.0, 0, dict_);


    //update fertility probabilities
    update_fertility_prob(ffert_count,0.0);

    //update distortion probabilities

    //a) cept-start (inter distortion)
    if (!nondeficient_) {

      IBM4CeptStartModel hyp_cept_start_prob;
      if (reduce_deficiency_)
	hyp_cept_start_prob = cept_start_prob_;

      for (uint x=0; x < cept_start_prob_.xDim(); x++) {

	std::cerr << "inter m-step(" << x << ",*)" << std::endl; 

	for (uint y=0; y < cept_start_prob_.yDim(); y++) {

	  double sum = 0.0;
	  for (uint d=0; d < cept_start_prob_.zDim(); d++) 
	    sum += fceptstart_count(x,y,d);
          
	  if (sum > 1e-305) {
          
	    const double inv_sum = 1.0 / sum;
	    
	    if (!reduce_deficiency_) {
	      for (uint d=0; d < cept_start_prob_.zDim(); d++) 
		cept_start_prob_(x,y,d) = std::max(1e-8,inv_sum * fceptstart_count(x,y,d));
	    }
	    else {
	      
	      for (uint d=0; d < cept_start_prob_.zDim(); d++) 
		hyp_cept_start_prob(x,y,d) = std::max(1e-15,inv_sum * fceptstart_count(x,y,d));
	      
	      double cur_energy = inter_distortion_m_step_energy(inter_distort_count,sparse_inter_distort_count(x,y),
								 cept_start_prob_,x,y);
	      double hyp_energy = inter_distortion_m_step_energy(inter_distort_count,sparse_inter_distort_count(x,y),
								 hyp_cept_start_prob,x,y);
            
	      if (hyp_energy < cur_energy) {
		for (uint d=0; d < cept_start_prob_.zDim(); d++) 
		  cept_start_prob_(x,y,d) = hyp_cept_start_prob(x,y,d);

		cur_energy = hyp_energy;
	      }
	    }
	  }
	  
	  if (reduce_deficiency_) 
	    inter_distortion_m_step(inter_distort_count,sparse_inter_distort_count(x,y),x,y);  
	}
      }
      
      par2nonpar_inter_distortion();
    }
    else {

      for (uint x=0; x < cept_start_prob_.xDim(); x++) {
        std::cerr << "nondeficient inter-m-step(" << x << ",*)" << std::endl;
        for (uint y=0; y < cept_start_prob_.yDim(); y++) {

          std::map<Math1D::Vector<uchar,uchar>,uint>& cur_map = nondef_cept_start_count(x,y);

	  std::vector<Math1D::Vector<uchar,uchar> > open_diff(cur_map.size());
	  std::vector<double> weight(cur_map.size());

          uint k=0;
          for (std::map<Math1D::Vector<uchar,uchar>,uint>::const_iterator it = cur_map.begin(); it != cur_map.end(); it++) {
	    open_diff[k] = it->first;
	    weight[k] = it->second;
            k++;
          }

          cur_map.clear();

          double cur_energy = nondeficient_inter_m_step_energy(fnondef_ceptstart_singleton_count,open_diff,weight,cept_start_prob_,x,y);

          double sum = 0.0;
          for (uint k=0; k < cept_start_prob_.zDim(); k++) {
            sum += fceptstart_count(x,y,k);
          }

          if (sum > 1e-300) {

            IBM4CeptStartModel hyp_cept_start_prob = cept_start_prob_;

            for (uint k=0; k < cept_start_prob_.zDim(); k++) {
              hyp_cept_start_prob(x,y,k) = std::max(1e-15,fceptstart_count(x,y,k) / sum);
            }

            double hyp_energy = nondeficient_inter_m_step_energy(fnondef_ceptstart_singleton_count,open_diff,weight,hyp_cept_start_prob,x,y);

            if (hyp_energy < cur_energy) {          
              cept_start_prob_ = hyp_cept_start_prob;
              cur_energy = hyp_energy;
            }
          }
          
          nondeficient_inter_m_step_with_interpolation(fnondef_ceptstart_singleton_count,open_diff,weight,x,y,cur_energy);
        }
      }
    }

   
    //b) within-cept (intra distortion)
    if (!nondeficient_) {

      IBM4WithinCeptModel hyp_withincept_prob;
      if (reduce_deficiency_)
	hyp_withincept_prob = within_cept_prob_;

      for (uint x=0; x < within_cept_prob_.xDim(); x++) {
	
	double sum = 0.0;
	for (uint d=0; d < within_cept_prob_.yDim(); d++)
	  sum += fwithincept_count(x,d);
	
	if (sum > 1e-305) {
	  
	  const double inv_sum = 1.0 / sum;
	  
	  if (!reduce_deficiency_) {
	    for (uint d=0; d < within_cept_prob_.yDim(); d++)
	      within_cept_prob_(x,d) = std::max(inv_sum * fwithincept_count(x,d),1e-8);
	  }
	  else {
	    for (uint d=0; d < within_cept_prob_.yDim(); d++)
	      hyp_withincept_prob(x,d) = std::max(1e-15,inv_sum * fwithincept_count(x,d));
	    
	    double cur_energy = intra_distortion_m_step_energy(intra_distort_count,within_cept_prob_,x);
	    double hyp_energy = intra_distortion_m_step_energy(intra_distort_count,hyp_withincept_prob,x);
	    
	    if (hyp_energy < cur_energy) {
	      for (uint d=0; d < within_cept_prob_.yDim(); d++)
		within_cept_prob_(x,d) = hyp_withincept_prob(x,d);
	    }
	  }
	}
        
	std::cerr << "intra-m-step(" << x << ")" << std::endl;
	if (reduce_deficiency_)
	  intra_distortion_m_step(intra_distort_count,x);
      }
      
      par2nonpar_intra_distortion();
    }
    else {

      IBM4WithinCeptModel hyp_withincept_prob = within_cept_prob_;

      for (uint x=0; x < within_cept_prob_.xDim(); x++) {
        std::cerr << "calling nondeficient intra-m-step(" << x << ")" << std::endl;

        std::map<Math1D::Vector<uchar,uchar>,uint>& cur_map = nondef_within_cept_count[x];

        std::vector<std::pair<Math1D::Vector<uchar,uchar>,double> > count(cur_map.size());

        uint k=0;
        for (std::map<Math1D::Vector<uchar,uchar>,uint>::const_iterator it = cur_map.begin(); it != cur_map.end(); it++) {
          std::pair<Math1D::Vector<uchar,uchar>,double> new_pair;

          count[k] = *it;
          k++;
        }

        cur_map.clear();

        double sum = 0.0;
        for (uint d=0; d < within_cept_prob_.yDim(); d++)
          sum += fwithincept_count(x,d);
	
        if (sum > 1e-305) {
            
          const double inv_sum = 1.0 / sum;
          for (uint d=0; d < within_cept_prob_.yDim(); d++)
            hyp_withincept_prob(x,d) = std::max(1e-15,inv_sum * fwithincept_count(x,d));

          double cur_energy = nondeficient_intra_m_step_energy(fnondef_withincept_singleton_count,count,within_cept_prob_,x);
          double hyp_energy = nondeficient_intra_m_step_energy(fnondef_withincept_singleton_count,count,hyp_withincept_prob,x);

          if (hyp_energy < cur_energy) {
	    for (uint d=0; d < within_cept_prob_.yDim(); d++)
	      within_cept_prob_(x,d) = hyp_withincept_prob(x,d); 

	    cur_energy = hyp_energy;
	  }
        }
        
        nondeficient_intra_m_step(fnondef_withincept_singleton_count,count,x);
      }
    }


    //c) sentence start prob
    if (use_sentence_start_prob_) {

      start_prob_m_step(sentence_start_count, sentence_start_parameters_);
      par2nonpar_start_prob(sentence_start_parameters_,sentence_start_prob_);
    }

    //DEBUG
#ifndef NDEBUG
    for (size_t s=0; s < source_sentence_.size(); s++) {
      
      long double align_prob = alignment_prob(s,best_known_alignment_[s]);
      
      if (isinf(align_prob) || isnan(align_prob) || align_prob == 0.0) {
	
	std::cerr << "ERROR: after parameter update: align-prob for sentence " << s << " has prob " << align_prob << std::endl;

        const SingleLookupTable& cur_lookup = get_wordlookup(source_sentence_[s],target_sentence_[s],wcooc_,
                                                             nSourceWords_,slookup_[s],aux_lookup);

	print_alignment_prob_factors(source_sentence_[s], target_sentence_[s], cur_lookup, best_known_alignment_[s]);

	exit(1);
      }
    }
#endif
    //END_DEBUG

    max_perplexity /= source_sentence_.size();

    //ICM STAGE
    if (fert_trainer == 0 && wrapper == 0 && !nondeficient_) { 
      //no point doing ICM in a transfer iteration
      //in nondeficient mode, ICM does well at decreasing the energy, but it heavily aligns to the rare words

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
	
	long double cur_distort_prob = (nondeficient_) ? nondeficient_distortion_prob(cur_source,cur_target,hyp_aligned_source_words) :
	  distortion_prob(cur_source,cur_target,hyp_aligned_source_words);
	
	for (uint j=0; j < curJ; j++) {

	  for (uint i = 0; i <= curI; i++) {
	    
	    const uint cur_aj = best_known_alignment_[s][j];
	    const uint cur_word = (cur_aj == 0) ? 0 : cur_target[cur_aj-1];

	    const uint new_target_word = (i == 0) ? 0 : cur_target[i-1];
	 
   
	    /**** dict ***/
	    //std::cerr << "i: " << i << ", cur_aj: " << cur_aj << std::endl;
	    
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

	      const long double hyp_distort_prob = (nondeficient_) ? nondeficient_distortion_prob(cur_source,cur_target,hyp_aligned_source_words) :
		distortion_prob(cur_source,cur_target,hyp_aligned_source_words);
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

	//clean up cache
	for (uint j=0; j < inter_distortion_cache_[curJ].size(); j++)
	  inter_distortion_cache_[curJ][j].clear();

      } //ICM-loop over sentences finished

      std::cerr << nSwitches << " changes in ICM stage" << std::endl; 
    }
    
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
    update_fertility_prob(ffert_count,0.0);

    //TODO: think about updating distortion here as well (will have to recollect counts from the best known alignments)

    std::string transfer = ((fert_trainer != 0 || wrapper != 0) && iter == 1) ? " (transfer) " : ""; 

    std::cerr << "IBM-4 max-perplex-energy in between iterations #" << (iter-1) << " and " << iter << transfer << ": "
              << max_perplexity << std::endl;
    if (possible_ref_alignments_.size() > 0) {
      
      std::cerr << "#### IBM-4-AER in between iterations #" << (iter-1) << " and " << iter << transfer << ": " << AER() << std::endl;
      std::cerr << "#### IBM-4-fmeasure in between iterations #" << (iter-1) << " and " << iter << transfer << ": " << f_measure() << std::endl;
      std::cerr << "#### IBM-4-DAE/S in between iterations #" << (iter-1) << " and " << iter << transfer << ": " 
                << DAE_S() << std::endl;
    }

    std::cerr << (((double) sum_iter) / source_sentence_.size()) << " average hillclimbing iterations per sentence pair" 
              << std::endl;
  }

  iter_offs_ = iter-1;
}


