#include "picgen.hpp"
#include <stdexcept>

std::optional<mpi::CartComm> make_cart( const apt::array<int,pic::DGrid>& dims, const apt::array<bool,pic::DGrid> periodic ) {
  std::optional<mpi::CartComm> cart_opt;
  int nprmy = 1;
  for ( auto i : dims )
    nprmy *= i;

  if ( nprmy > mpi::world.size() )
    throw std::invalid_argument("Size of the Cartesian topology exceeds the size of world!");

  // simply use first few members in mpi::world as primaries
  bool is_prmy = mpi::world.rank() < nprmy;
  auto comm_tmp = mpi::world.split( {is_prmy}, mpi::world.rank() );
  if ( is_prmy ) {
    std::vector<int> d(pic::DGrid);
    std::vector<bool> p(pic::DGrid);
    for ( int i = 0; i < pic::DGrid; ++i ) {
      d[i] = dims[i];
      p[i] = periodic[i];
    }
    cart_opt.emplace( *comm_tmp, d, p );
  }
  return cart_opt;
}

int main() {

  mpi::initialize();

  { // use block to force destruction of potential mpi communicators before mpi::finalize
    io::project_name = pic::project_name;
    io::init_this_run_dir( pic::datadir_prefix );
    auto cart_opt = make_cart(pic::dims, pic::periodic);

    pic::Simulator< pic::DGrid, pic::real_t, particle::Specs, pic::ShapeF, pic::real_j_t, pic::Metric >
      sim( pic::supergrid, cart_opt, pic::guard );

    int init_timestep = sim.load_initial_condition<pic::InitialCondition>();
    sim.set_rng_seed( init_timestep + mpi::world.rank() );

    for ( int ts = init_timestep; ts < init_timestep + pic::total_timesteps; ++ts ) {
      sim.evolve( ts, pic::dt );
    }
  }

  mpi::finalize();
  return 0;
}
