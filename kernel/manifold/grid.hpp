#ifndef  _MANIFOLD_GRID_HPP_
#define  _MANIFOLD_GRID_HPP_

#include "apt/array.hpp"
#include "apt/index.hpp"

// NOTE convention: the zero of all indices exposed to users start at the first cell in BULK. In other words, guard cells have indices outside [0, dim_of_bulk). Guard is important only when converting from vectorial to linear index, which can be encapsulated in a dedicated function

// guard, indent are controlled by fields directly, where they are collectively called margin cells.

// namespace knl {
//   template < typename T >
//   struct SuperGrid1D {
//   private:
//     int _dim;
//     T _delta;
//     T _lower;

//   public:
//     constexpr SuperGrid1D() noexcept: SuperGrid1D( 0.0, 1.0, 1 ) {}

//     constexpr SuperGrid1D( T lower, T upper, int dim ) noexcept
//       : _dim(dim),
//         _delta( (upper - lower) / dim ),
//         _lower(lower) {}

//     constexpr int dim() const noexcept { return _dim; }
//     constexpr T delta() const noexcept { return _delta; }

//     constexpr T lower() const noexcept { return _lower; }
//     constexpr T upper() const noexcept { return absc(dim()); }

//     // abscissa
//     constexpr T absc( int i, T shift_from_lb = 0.0 ) const noexcept {
//       return  _lower + delta() * ( i + shift_from_lb );
//     }
//   };

// }

// namespace knl {
//   template < typename T >
//   struct SubGrid1D {
//   private:
//     const SuperGrid1D<T>& _supergrid;
//     int _anchor; // the cell in the super gridline
//     int _dim;

//   public:
//     constexpr SubGrid1D( const SuperGrid1D<T>& supergrid, int anchor, int dim ) noexcept
//       : _supergrid(supergrid), _anchor(anchor), _dim(dim) {}

//     constexpr int dim() const noexcept { return _dim; }
//     constexpr T delta() const noexcept { return _supergrid.delta(); }

//     constexpr T lower() const noexcept { return _supergrid.absc(_anchor); }
//     constexpr T upper() const noexcept { return _supergrid.absc(_anchor + _dim); }
//   };
// }

namespace mani {
  template < typename T >
  struct Grid1D {
  private:
    int _dim;
    T _delta;
    T _lower;

  public:
    constexpr Grid1D() noexcept: Grid1D( 0.0, 1.0, 1 ) {}

    constexpr Grid1D( T lower, T upper, int dim ) noexcept
      : _dim(dim),
        _delta( (upper - lower) / dim ),
        _lower(lower) {}

    constexpr int dim() const noexcept { return _dim; }
    constexpr T delta() const noexcept { return _delta; }

    constexpr T lower() const noexcept { return _lower; }
    constexpr T upper() const noexcept { return absc(dim()); }

    // abscissa
    constexpr T absc( int i, T shift_from_lb = 0.0 ) const noexcept {
      return  _lower + delta() * ( i + shift_from_lb );
    }

    constexpr void clip( int i_start, int extent ) noexcept {
      _lower = absc( i_start );
      _dim = extent;
    }

    constexpr Grid1D divide( int num_pieces, int ith_piece ) const noexcept {
      Grid1D res = *this;
      int dim = _dim / num_pieces;
      res.clip( ith_piece * dim, dim );
      return res;
    }
  };

}

namespace mani {
  template < typename T, int DGrid >
  using Grid = apt::array< Grid1D<T>, DGrid >;

  template < typename T, int DGrid >
  constexpr apt::Index<DGrid> dims(const Grid<T,DGrid>& grid) noexcept {
    apt::Index<DGrid> ext;
    for ( int i = 0; i < DGrid; ++i )
      ext[i] = grid[i].dim();
    return ext;
  }
}

#endif
