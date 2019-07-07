#include "particle/updater.hpp"
#include "msh/mesh_shape_interplay.hpp"
#include "manifold/curvilinear_impl.hpp"
#include "manifold/grid.hpp"

#ifdef PIC_DEBUG
#include "debug/debugger.hpp"
#include "logger/logger.hpp"
#endif

namespace particle {
  template < int DGrid,
             typename Real,
             template < typename > class PtcSpecs,
             typename ShapeF,
             typename RealJ,
             typename Metric
             >
  Updater< DGrid, Real, PtcSpecs, ShapeF, RealJ, Metric >
  ::Updater( const mani::Grid< Real, DGrid >& localgrid, const util::Rng<Real>& rng,
             const map<Properties>& properties,
             const ForceGen_t& force_gen, const ScatGen_t& scat_gen )
    : _localgrid(localgrid), _rng(rng), _properties(properties), _force_gen(force_gen), _scat_gen(scat_gen) {}
}

namespace particle {
  template < int DGrid,
             typename Real,
             template < typename > class PtcSpecs,
             typename ShapeF,
             typename RealJ,
             typename Metric
             >
  void Updater< DGrid, Real, PtcSpecs, ShapeF, RealJ, Metric >
  ::update_species( species sp,
                    array<Real,PtcSpecs>& sp_ptcs,
                    field::Field<RealJ,3,DGrid>& J,
                    Real dt,
                    const field::Field<Real,3,DGrid>& E,
                    const field::Field<Real,3,DGrid>& B
                    ) {
    if ( sp_ptcs.size() == 0 ) return;

    const auto& prop = _properties.at(sp);

    using Ptc = typename array<Real,PtcSpecs>::particle_type;

    auto update_p = _force_gen(sp);

    auto* scat = _scat_gen(sp);

    constexpr auto shapef = ShapeF();
    auto charge_over_dt = static_cast<Real>(prop.charge_x) / dt;

    auto update_q =
      [is_massive=(prop.mass_x != 0)] ( auto& ptc, Real dt ) {
        return Metric::geodesic_move( ptc.q(), ptc.p(), dt, is_massive );
      };

    for ( auto ptc : sp_ptcs ) { // TODOL sematics, check ptc is proxy
      if( !ptc.is(flag::exist) ) continue;
#ifdef PIC_DEBUG
      apt::Vec<Real,PtcSpecs<Real>::Dim> q0 = ptc.q();
      apt::Vec<Real,PtcSpecs<Real>::Dim> p0 = ptc.p();

      auto show_ptc =
        [&](const auto& ptc) {
          lgr::file << "ptc.q() = (" << ptc.q()[0];
          for ( int i = 1; i < PtcSpecs<Real>::Dim; ++i )
            lgr::file << ", " << ptc.q()[i];
          lgr::file << ")" << std::endl;

          lgr::file << "ptc.p() = (" << ptc.p()[0];
          for ( int i = 1; i < PtcSpecs<Real>::Dim; ++i )
            lgr::file << ", " << ptc.p()[i];
          lgr::file << ")" << std::endl;

          lgr::file << "ptc q0 = " << q0 << std::endl;
          lgr::file << "ptc p0 = " << p0 << std::endl;
        };

      if(debug::has_nan(ptc)) {
        lgr::file << "ts=" << debug::timestep << ", wr=" << debug::world_rank << " NANBEGINNING, code=" << debug::has_nan(ptc);
        show_ptc(ptc);
        throw std::runtime_error("NAN at rank" + std::to_string(debug::world_rank));
      }
#endif

      auto q0_std = msh::to_standard( _localgrid, ptc.q() );
      auto E_itpl = msh::interpolate( E, q0_std, shapef );
      auto B_itpl = msh::interpolate( B, q0_std, shapef );

      apt::Vec<Real,PtcSpecs<Real>::Dim> dp = -ptc.p();
      update_p( ptc, dt, E_itpl, B_itpl );
#ifdef PIC_DEBUG
      if(debug::has_nan(ptc)) {
        lgr::file << "ts=" << debug::timestep << ", wr=" << debug::world_rank << " NANUPDATEP, code=" << debug::has_nan(ptc) << std::endl;;
        show_ptc(ptc);
        throw std::runtime_error("NAN at rank" + std::to_string(debug::world_rank));
      }
#endif

      if ( scat ) {
        dp += ptc.p(); // ptc.p() is the updated one but still based on the same location

        bool is_eligible = true;
        for ( const auto& elig : scat->eligs ) {
          if ( !(*elig)(ptc) ) {
            is_eligible = false;
            break;
          }
        }
        if ( is_eligible ) {
          for ( const auto& chnl : scat->channels ) {
            if ( auto param = (*chnl)(ptc, prop, dp, dt, B_itpl, _rng) ) {
              scat->impl( std::back_inserter(_buf), ptc, *param );
              break;
            }
          }
        }
#ifdef PIC_DEBUG
        if(debug::has_nan(ptc)) {
          lgr::file << "ts=" << debug::timestep << ", wr=" << debug::world_rank << " NANSCAT, code=" << debug::has_nan(ptc) << std::endl;;
          show_ptc(ptc);
          throw std::runtime_error("NAN at rank" + std::to_string(debug::world_rank));
        }
#endif
      }

      // NOTE q is updated, starting from here, particles may be in the guard cells.
      auto dq = update_q( ptc, dt );
#ifdef PIC_DEBUG
      if(debug::has_nan(ptc)) {
        lgr::file << "ts=" << debug::timestep << ", wr=" << debug::world_rank << " NANUPDATEQ, code=" << debug::has_nan(ptc) << std::endl;;
        show_ptc(ptc);
        throw std::runtime_error("NAN at rank" + std::to_string(debug::world_rank));
      }
#endif
      // TODO pusher handle boundary condition. Is it needed?
      if ( prop.charge_x != 0 ) {
        // change dq to q1 in the coordinate space. NOTE it is not necessarily the same as ptc.q()
        for ( int i = 0; i < DGrid; ++i ) {
          dq[i] /= _localgrid[i].delta();
          dq[i] += q0_std[i];
        }
        for ( int i = DGrid; i < PtcSpecs<Real>::Dim; ++i )
          dq[i] += q0_std[i];
        msh::deposit( J, charge_over_dt, shapef, q0_std, std::move(dq) );
      }

    }
  }
}

namespace particle {
  template < int DGrid,
             typename Real,
             template < typename > class PtcSpecs,
             typename ShapeF,
             typename RealJ,
             typename Metric >
  void Updater< DGrid, Real, PtcSpecs, ShapeF, RealJ, Metric >
  ::operator() ( map<array<Real,PtcSpecs>>& particles,
                 field::Field<RealJ,3,DGrid>& J,
                 const field::Field<Real,3,DGrid>& E,
                 const field::Field<Real,3,DGrid>& B,
                 Real dt, int timestep ) {

    for ( auto&[ sp, ptcs ] : particles ) {
      update_species( sp, ptcs, J, dt, E, B );
    }

    { // Put particles where they belong after scattering
      for ( int i = 0; i < _buf.size(); ++i ) {
        auto this_sp = _buf[i].template get<species>();
        particles[this_sp].push_back(std::move(_buf[i]));
      }
      _buf.resize(0);
    }

    { // NOTE rescale Jmesh back to real grid delta
      Real dV = 1.0;
      for ( int i = 0; i < DGrid; ++i ) dV *= _localgrid[i].delta();

      for ( int i = 0; i < DGrid; ++i ) {
        Real tmp = _localgrid[i].delta() / dV;
        for ( auto& elm : J[i].data() ) elm *= tmp;
      }

      for ( int i = DGrid; i < 3; ++i ) {
        for ( auto& elm : J[i].data() ) elm /= dV;
      }
    }

  }

}
