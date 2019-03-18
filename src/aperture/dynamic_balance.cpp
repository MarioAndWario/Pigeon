#include "./dynamic_balance.hpp"
#include "./ensemble.hpp"
#include "parallel/mpi++.hpp"
#include "particle/c_particle.hpp"
#include "apt/priority_queue.hpp" // used in calc_new_nprocs

using load_t = unsigned long long;

namespace aperture::impl {
  auto bifurcate( const mpi::Comm& parent, bool color ) {
    mpi::Comm child ( *(parent.split(color, parent.rank())) );
    std::optional<mpi::InterComm> itc;
    if ( child.size() == parent.size() ) return std::make_tuple( child, itc );

    auto is_found =
      []( const auto& ranks, int r ) {
        for ( const auto elm : ranks )
          if ( elm == r ) return true;
        return false;
      };

    // NOTE assume ranks_in_child.size() < ranks_in_parent.size()
    auto find_first_in_other =
      [is_found] ( const auto& ranks_in_child, auto parent_size ) {
        for ( int i = 0; i < parent_size; ++i )
          if ( !is_found(ranks_in_child, i) ) return i;
      };

    // RATIONALE the one with smallest rank in parent in each branch becomes the leader
    int local_leader = child.group().translate( 0, parent.group() ); // using parent.rank() in calling parent.split guarantees the correctness
    int remote_leader = find_first_in_other(child.group().translate_all(parent.group()), parent.size());
    itc.emplace(mpi::InterComm(child, local_leader, parent, remote_leader, 80));

    return std::make_tuple( child, itc );
  }
}

// calc_nprocs_new
namespace aperture::impl {
  void calc_new_nprocs_impl ( std::vector<unsigned int>& nproc, const std::vector<load_t>& load, load_t ave_load, const load_t target_load ) {
    // nproc is valid to start with
    const unsigned int nens = nproc.size();

    ave_load = ( ave_load > target_load ) ? ave_load : target_load;

    int total_surplus = 0;
    for ( int i = 0; i < nens; ++i ) {
      total_surplus += nproc[i]; // old nproc[i]
      nproc[i] = load[i] / ave_load;
      nproc[i] = (nproc[i] > 1) ? nproc[i] : 1;
      total_surplus -= nproc[i]; // new nproc[i]
    }

    if ( total_surplus > 0 ) {
      // have more than needed. Leverage idles to mitigate the most stressed
      apt::priority_queue<load_t> pq; // store average load in pq
      for ( int i = 0; i < nens; ++i )
          pq.push( i, load[i] / nproc[i] );

      int count = total_surplus;
      while( count > 0 && !pq.empty() ) {
        auto [i,avld] = pq.top(); // avld means ave_load
        if ( avld > target_load ) {
          pq.pop();
          ++nproc[i];
          load_t avld_new = load[i] / nproc[i];
          if ( avld_new > target_load )
            pq.push( i, avld_new );
          --count;
        } else
          pq.pop();
      }
    } else if ( total_surplus < 0 ) { //
      // have less than needed. The richest needs to spit out
      apt::priority_queue< load_t, std::greater<load_t> > pq; // store average load in pq
      for ( int i = 0; i < nens; ++i )
          pq.push( i, load[i] / nproc[i] );

      int count = -total_surplus;
      while( count > 0 ) {
        int i = std::get<0>(pq.top());
        pq.pop();
        if ( nproc[i] > 1 ) {
          --nproc[i];
          pq.push( i, load[i] / nproc[i] );
          --count;
        }
      }
    }
  }

  // loads = [ ens_tot_load_0, ens_size_0, ens_tot_load_1, ens_size_1... ]
  // actual total_nprocs = max( total_num_procs, accummulated_value_from_loads)
  std::vector<unsigned int> calc_new_nprocs( const std::vector<load_t>& loads_and_nprocs, load_t target_load, const unsigned int max_nprocs = 0 ) {
    // RATIONALE The following gives a close to the, if not THE, most optimal deployment
    // 1. Priority Queue is used to accommodate excess or leftover of processes if any
    // 2. Edge cases:
    //    - make sure every ensemble retains at least one process
    //    - when the load is below target_load, priority queue should do nothing to that ensemble even when there's idles
    //    - note if load = 0 on some ensembles
    //    - only one ensemble

    // first split loads into loads and nprocs
    const auto nens = loads_and_nprocs.size() / 2;
    if ( nens == 1 ) return { loads_and_nprocs[1]};

    load_t total_load = 0;
    unsigned int total_nprocs = 0;

    std::vector<load_t> load(nens, 0);
    load.shrink_to_fit();
    std::vector<unsigned int> nproc(nens, 0);
    nproc.shrink_to_fit();

    for ( int i = 0; i < nens; ++i ) {
      load[i] = loads_and_nprocs[2*i];
      nproc[i] = loads_and_nprocs[2*i+1];
      total_load += loads_and_nprocs[2*i];
      total_nprocs += loads_and_nprocs[2*i + 1];
    }
    total_nprocs = ( total_nprocs > max_nprocs ? total_nprocs : max_nprocs );

    const load_t ave_load_least_possible = (total_load / total_nprocs) + 1; // ceiling

    int N = 1; // TODOL does iteration give better results?
    while( N-- )
      calc_new_nprocs_impl( nproc, load, ave_load_least_possible, target_load );

    return nproc;
  }
}

// assign_labels
namespace aperture::impl{
  std::optional<int> assign_labels( const mpi::InterComm& job_market,
                                    const std::vector<int>& deficits,
                                    std::optional<int> cur_label ) {
    // RATIONALE job_market contains all primaries and all idles including those just fired from shrinking ensembles. The goal here is for all primaries to coordinate whom to send their labels to.
    // NOTE `deficits` should be valid already. This means
    //   - deficits[i] >= 0
    //   - the sum of deficits <= num_idles. In the case of inequality, idles will be picked by their rank in the job_market.
    // Edge cases:
    //   - no idles

    if ( job_market.remote_size() == 0 ) return {};

    std::optional<int> new_label;

    if ( cur_label ) { // primary
      new_label = cur_label;
      int num_offers = 0;
      for ( int i = 0; i < deficits.size(); ++i ) num_offers += deficits[i];

      int root = ( 0 == job_market.rank() ) ? MPI_ROOT : MPI_PROC_NULL;
      job_market.broadcast( root, &num_offers, 1 );

      int myrank = job_market.rank();
      int num_offers_ahead = 0;
      for ( int i = 0; i < myrank; ++i )
        num_offers_ahead += deficits[i];

      int label = *cur_label;
      std::vector<mpi::Request> reqs(deficits[myrank]);
      for ( int i = 0; i < deficits[myrank]; ++i )
        reqs[i] = job_market.Isend(num_offers_ahead + i, 835, &label, 1 );

      mpi::waitall(reqs);

    } else {
      int num_offers = 0;
      job_market.broadcast(0, &num_offers, 1 );
      if ( job_market.rank() < num_offers ) {
        int label = 0;
        job_market.recv(MPI_ANY_SOURCE, 835, &label, 1);
        new_label.emplace(label);
      }
    }

    return new_label;
  };
}

namespace aperture::impl {
  struct balance_code {
    static constexpr int send = 1;
    static constexpr int none = 0;
    static constexpr int recv = -1;
  };

  std::vector<int> get_ptc_num_surplus ( const std::vector<load_t>& nums_ptc ) {
    // NOTE: surplus = actual number - expected number
    std::vector<int> spls( nums_ptc.size() );
    load_t num_tot = 0;
    for ( auto x : nums_ptc ) num_tot += x;

    load_t average = num_tot / nums_ptc.size();
    int remain = num_tot % nums_ptc.size();
    for ( unsigned int rank = 0; rank < nums_ptc.size(); ++rank ) {
      load_t num_exp = average + ( rank < remain );
      spls[rank] = nums_ptc[rank] - num_exp;
    }

    return spls;
  }

  // The ith element of nums_adjust, which should be ordered by ensemble ranks, specifies the number of particles ( could be positive, zero, or negative ) to be adjusted for the process i. Postive indicates sending. The constraint is that the sum of nums_adjust elements must be zero in order to conserve total particle number. The return value contains the actual sendrecv scheme for the corresponding process. Each scheme is of the format, action, member1, number1, member2, number2, etc, where numbers here are all positive.
  std::vector<int> get_instr( const std::vector<int>& nums_surplus, int my_rank ) {
    std::vector<std::vector<int>> instructions ( nums_surplus.size() );
    // split the input array into positive(send), zero, and negative(recv)
    std::vector<int> procs_send;
    std::vector<int> procs_recv;
    for ( unsigned int i = 0; i < nums_surplus.size(); ++i ) {
      if ( 0 == nums_surplus[i] )
        instructions[i] = { balance_code::none };
      else if ( nums_surplus[i] > 0 ) {
        instructions[i] = { balance_code::send };
        procs_send.push_back(i);
      }
      else {
        instructions[i] = { balance_code::recv };
        procs_recv.push_back(i);
      }
    }

    // match postives with negatives
    auto it_s = procs_send.begin();
    auto it_r = procs_recv.begin();
    int num_s = it_s != procs_send.end() ? std::abs( nums_surplus[*it_s] ) : 0;
    int num_r = it_r != procs_recv.end() ? std::abs( nums_surplus[*it_r] ) : 0;

    while ( it_s != procs_send.end() && it_r != procs_recv.end() ) {
      auto& instr_s = instructions[ *it_s ];
      auto& instr_r = instructions[ *it_r ];
      int num_transfer = std::min( num_s, num_r );
      instr_s.push_back( *it_r );
      instr_s.push_back(num_transfer);
      instr_r.push_back( *it_s );
      instr_r.push_back(num_transfer);

      num_s -= num_transfer;
      num_r -= num_transfer;
      if ( 0 == num_s && (++it_s) != procs_send.end() )
        num_s = std::abs( nums_surplus[*it_s] );
      if ( 0 == num_r && (++it_r) != procs_recv.end() )
        num_r = std::abs( nums_surplus[*it_r] );
    }

    return instructions[my_rank];

  }

}

namespace aperture {
  template < typename T, int DPtc, typename state_t >
  void detailed_balance ( particle::array<T, DPtc, state_t>& ptcs,
                          const mpi::Comm& intra ) {
    load_t my_load = ptcs.size();
    auto loads = intra.allgather(&my_load, 1);
    auto instr = impl::get_instr( impl::get_ptc_num_surplus( loads ), intra.rank() );

    if ( impl::balance_code::none == instr[0] ) return;

    // Start from the back of the particle array for both send and recv actions
    int position = ptcs.size();
    const int num_comms = ( instr.size() - 1 ) / 2; // number of communications to make

    std::vector<mpi::Request> reqs(num_comms);

    std::vector<particle::cParticle<T,DPtc,state_t>> buffer;

    for ( int i = 0; i < num_comms; ++i ) {
      int other_rank = instr[ 2 * i + 1 ];
      int num = instr[ 2 * i + 2 ];
      buffer.resize(num);
      int tag = 147;
      if ( impl::balance_code::send == instr[0] ) {
        position -= num;
        for ( int j = 0; j < num; ++j ) buffer[j] = ptcs[position + j];
        reqs[i] = intra.Isend( other_rank, tag, buffer.data(), num );
      } else {
        // TODOL double check if using Irecv/Isend would cause race condition?
        // NOTE: using Irecv here causes hanging behavior on some platforms. So we use recv.
        intra.recv( other_rank, tag, buffer.data(), num );
        ptcs.resize(ptcs.size() + num);
        for ( int j = 0; j < num; ++j ) ptcs[position + j] = buffer[j];
        position += num;
      }
    }
    mpi::waitall(reqs);
    if ( impl::balance_code::send == instr[0] ) ptcs.resize( position );
  }
}

namespace aperture {

  template < typename T, int DPtc, typename state_t, int DGrid >
  void dynamic_adjust( particle::map<particle::array<T, DPtc, state_t>>& particles,
                       std::optional<Ensemble<DGrid>>& ens_opt,
                       const std::optional<mpi::CartComm>& cart_opt,
                       unsigned int target_load ) {
    // NOTE deficit = desired number - current number
    std::vector<int> nproc_deficit; // significant only at primaries

    bool is_leaving = false;
    { // Step 1. based on particle load, primaries figure out the surpluses. If an ensemble has surplus > 0, the primary will flag the last few processes to be leaving the ensemble.
      if ( ens_opt ) {
        load_t my_tot_load = 0; // a process within an ensemble
        for ( auto[sp,ptcs] : particles ) {
          if ( sp == particle::species::photon )
            my_tot_load += ptcs.size() / 3; // empirically photon performance is about 3 times faster than that of a particle
          else
            my_tot_load += ptcs.size();
        }

        const auto& intra = ens_opt -> intra;
        intra.reduce<mpi::by::SUM, mpi::IN_PLACE>(&my_tot_load, 1, ens_opt->chief);
        int new_ens_size = 0;
        if ( cart_opt ) {
          auto& nprocs_new = nproc_deficit;
          nprocs_new.resize(cart_opt->size());
          nprocs_new.shrink_to_fit();
          load_t my_load[2] = { my_tot_load, intra->size() };
          auto loads_and_nprocs = cart_opt->allgather(my_load, 2);
          nprocs_new = impl::calc_new_nprocs( loads_and_nprocs, target_load, mpi::world.size() );
          new_ens_size = nprocs_new[cart_opt->rank()];
          for ( int i = 0; i < nproc_deficit.size(); ++i )
            nproc_deficit[i] -= loads_and_nprocs[2*i+1];
        }
        intra.broadcast(&new_ens_size, 1, ens_opt->chief);
        if ( intra.rank() >= new_ens_size ) is_leaving = true;
      }
    }

    { // Step 2. Leaving processes clear up and return to idle
      if ( ens_opt ) {
        auto[comm, itc_opt] = impl::bifurcate( ens_opt->intra, is_leaving );
        if ( itc_opt ) {
          const auto& itc = *itc_opt;

          std::vector<particle::cParticle<T,DPtc,state_t>> buf;

          auto scatter =
            [&] (auto& ptc_array, int root) {
              int count = 0;
              if ( root == MPI_ROOT ) {
                const auto size = ptc_array.size();
                count = ( size / itc.remote_size() ) + 1; // will do padding
                itc.broadcast( &count, 1, root );
                buf.resize( count * itc.remote_size() );
              } else {
                itc.broadcast( &count, 1, root );
                buf.resize( count );
              }


              if ( root == MPI_ROOT ) {
                for ( int i = 0; i < ptc_array.size(); ++i )
                  buf[i] = ptc_array[i];
              }

              itc.scatter(root, buf.data(), count );

              if ( root != MPI_ROOT && root != MPI_PROC_NULL ) {
                ptc_array.resize( ptc_array.size() + count );
                for ( int i = 0; i < count; ++i )
                  ptc_array[i] = buf[i];
              }
            };

          for ( auto[sp, ptcs] : particles ) {
            if (is_leaving) {
              for ( int i = 0; i < itc.size(); ++i ) {
                if ( itc.rank() == i ) scatter( ptcs, MPI_ROOT );
                else scatter( ptcs, MPI_PROC_NULL );
              }
            } else {
              for ( int i = 0; i < itc.remote_size(); ++i )
                scatter( ptcs, i );
            }
          }

          if ( is_leaving ) {
            for ( auto[sp, ptcs] : particles ) ptcs = {}; // clear out particles
            ens_opt.reset(); // CRUCIAL!!
          }
        }
      }
      if ( cart_opt ) { // clear negative deficits
        for ( auto& x : nproc_deficit ) x = ( x > 0 ) ? x : 0;
      }
    }

    { // Step 3. Assign ensemble labels to all processes and update Ensemble. Need an intercommunicator between all primaries and all idles
      std::optional<unsigned int> new_label;
      bool is_replica = ens_opt && !cart_opt;
      auto prmy_idle_comm = std::get<0>( impl::bifurcate( mpi::world, is_replica ) );
      if ( is_replica ) new_label.emplace(ens_opt->label);
      else {
        bool is_idle = !ens_opt;
        auto[comm, job_market] = impl::bifurcate( prmy_idle_comm, is_idle );
        std::optional<unsigned int> cur_label {ens_opt ? ens_opt->label : nullptr };
        new_label = impl::assign_labels( *job_market, nproc_deficit, cur_label );
      }
      auto new_ens_intra_opt = mpi::world.split( new_label, mpi::world.rank() );

      auto new_ens_opt = create_ensemble<DGrid>( cart_opt, new_ens_intra_opt );
      ens_opt.swap(new_ens_opt);
    }

    { // Step 4. Perform a complete rebalance on all ensembles together
      if ( ens_opt ) {
        for ( auto[sp, ptcs] : particles )
          detailed_balance(ptcs, ens_opt->intra);
      }
    }

    { // Step 5. After locale is updated, sort particles and photons in case of actions such as annihilation FIXME is this needed?
    }

  }
}
