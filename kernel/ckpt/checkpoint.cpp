#include "ckpt/checkpoint_impl.hpp"
#include "gen.hpp"

namespace ckpt {
  using namespace pic;

  template
  std::string save_checkpoint( std::string prefix, const int num_parts,
                               const std::optional<dye::Ensemble<DGrid>>& ens_opt,
                               int timestep,
                               const field::Field<real_t, 3, DGrid>& E,
                               const field::Field<real_t, 3, DGrid>& B,
                               const particle::map<particle::array<real_t,particle::Specs>>& particles
                               );
}
