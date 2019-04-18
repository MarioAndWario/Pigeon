#ifndef  _TRAITS_HPP_
#define  _TRAITS_HPP_

#include "kernel/shapef.hpp"
#include "particle/species_predef.hpp"
#include "kernel/coordsys_predef.hpp"

#include "apt/array.hpp"
#include "apt/pair.hpp"

// TODO group particle type info into one struct
namespace traits {
  using real_t = double;
  using ptc_state_t = unsigned long long;

  constexpr int DGrid = 2;

  constexpr int DPtc = 3;

  constexpr apt::array< apt::pair<real_t>, 3 >
  grid_limits {{ {0.0, 100.0}, {0.0, 50.0}, {0.0, 20.0} }};

  constexpr apt::array<int, 3> grid_dims { 10, 5, 2 };

  constexpr int guard = 1; // TODOL more useful is Order of field_solver and shape

  using ShapeF = knl::shapef_t<knl::shape::Cloud_In_Cell>;

  constexpr auto coordinate_system = knl::coordsys::Cartesian;

  constexpr unsigned int ion_mass = 5;

  using real_j_t = long double;
};

#endif
