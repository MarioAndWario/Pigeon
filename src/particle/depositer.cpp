#include "particle/depositer.hpp"
#include "apt/numeric.hpp"
#include "apt/vec.hpp"

namespace esirkepov :: impl {
  template < typename T >
  inline T calcW_2D( T sx0, T sx1, T sy0, T sy1 ) noexcept {
    return ( ( 2 * sx1 + sx0 ) * sy1 + ( sx1 * 2 * sx0 ) * sy0 ) / 6.0;
  }

  template < typename T >
  inline T calcW( T sx0, T sx1, T sy0, T sy1, T sz0, T sz1 ) noexcept {
    return (sx1 - sx0) * calcW_2D(sy0, sy1, sz0, sz1);
  }

  template < typename E, typename T = apt::element_t<E>, int N = apt::ndim_v<E> >
  auto vec_to_array( const apt::VecExpression<E>& vec ) {
    std::array<T,N> arr;
    apt::foreach<0,N>([](auto& a, const auto& b){ a = b; }, arr, vec );
    return arr;
  }

  template < typename T, int DGrid, class ShapeRange, int DField >
  class ShapeRangeIterator {
    static_assert( DField == 3 );
  private:
    int _I = 0;
    const ShapeRange& _sr;

    apt::Vec<int, DGrid> ijk;
    std::array<T, DGrid> s0;
    std::array<T, DGrid> s1;
    std::array<T, DField> W;


  public:
    using difference_type = int;
    using value_type = void;
    using reference = std::tuple< std::array<int, DGrid>, const std::array<T, DField>& >;
    using pointer = void;
    using iterator_category = std::forward_iterator_tag;

    ShapeRangeIterator( int I, const ShapeRange& sr ) noexcept : _I(I), _sr(sr) {}

    inline bool operator!= ( const ShapeRangeIterator& other ) const noexcept {
      return _I != other._I;
    }

    difference_type operator-( const ShapeRangeIterator& other ) const noexcept {
      return _I - other._I;
    }

    auto& operator++() noexcept { ++_I; return *this; }
    auto operator++(int) noexcept {
      auto res = ShapeRangeIterator(_I, _sr); ++_I; return res;
    }

    // TODO make sure ShapeF can be passed in as constexpr. This may be used to optimize the ever-checking away.
    reference operator*() noexcept {
      const auto& stride = _sr._stride;

      auto f_calc_s0s1 =
        [&shapef = _sr.shapef] ( auto& s0, auto& s1, const auto& index,
                                 const auto& sep1_b, const auto& dq ) noexcept {
          s0 = shapef( index + sep1_b + dq );
          s1 = shapef( index + sep1_b );
        };

      if constexpr ( DGrid >= 2 ) {
          std::get<0>(ijk) = _I % std::get<0>(stride);
          apt::foreach<0,1>( f_calc_s0s1, s0, s1, ijk, _sr._sep1_b, _sr._dq );

          if ( std::get<0>(ijk) != 0 ) goto CALCW;
          std::get<1>(ijk) = ( _I % std::get<1>(stride) ) / std::get<0>(stride);
          apt::foreach<1,2>( f_calc_s0s1, s0, s1, ijk, _sr._sep1_b, _sr._dq );

          if constexpr ( DGrid == 3 ) {
              if ( std::get<1>(ijk) != 0 ) goto CALCW;
              std::get<2>(ijk) = _I / stride[1];
              apt::foreach<2,3>( f_calc_s0s1, s0, s1, ijk, _sr._sep1_b, _sr._dq );
            }

        CALCW:

          if constexpr ( DGrid == 2 ) {
              W[0] = calcW( s0[0], s1[0], s0[1], s1[1], 1.0, 1.0 );
              W[1] = calcW( s0[1], s1[1], 1.0, 1.0, s0[0], s1[0] );
              W[2] = calcW( 0.0, 1.0, s0[0], s1[0], s0[1], s1[1] );
            } else {
            W[0] = calcW( s0[0], s1[0], s0[1], s1[1], s0[2], s1[2] );
            W[1] = calcW( s0[1], s1[1], s0[2], s1[2], s0[0], s1[0] );
            W[2] = calcW( s0[2], s1[2], s0[0], s1[0], s0[1], s1[1] );
          }

        } else {
        static_assert( DGrid > 1 && DGrid < 4 );
      }

      return std::forward_as_tuple(vec_to_array(_sr._I_b + ijk), W );
    }

  };


  template < typename T, int DGrid, typename ShapeF, int DField >
  class ShapeRange {
  private:
    const ShapeF& shapef;
    apt::Vec<T,DGrid> _dq; // NOTE DGrid, not DPtc
    apt::Vec<int, DGrid> _I_b;
    apt::Vec<T, DGrid> _sep1_b;
    apt::Vec<int, DGrid> _stride;

  public:
    friend class ShapeRangeIterator<T, DGrid, ShapeRange, DField >;
    using sr_iterator = ShapeRangeIterator<T, DGrid, ShapeRange, DField >;

    template < typename E1, typename E2, typename Grid >
    ShapeRange( const apt::VecExpression<E1>& q1_abs,
                const apt::VecExpression<E2>& dq_abs,
                const Grid& grid,
                const ShapeF& shapefunc )
      : shapef(shapefunc) {
      // RATIONALE: assume the offset = 0.5 in the dimension. The native grid is one offset by 0.5 with respect to the original one.
      // - q1 is relative position in the native grid. q1 starts at 0.0
      // - the contributing cells in the native grid are [ int(q1 - sf.r) + 1,  int(q1 + sf.r) + 1 )
      // - index_original = index_native + ( q1 - int(q1) >= 0.5 )
      // Now we have q0 and q1, the final range of contributing cells is the union of individual ones

      apt::foreach<0, DGrid>
        ( [&sf=shapef]( auto& dq_rel, auto& ind_b, auto& sep1_b, auto& stride,
                        auto xmin, auto xmax, const auto& gl ) noexcept {
            // initially, xmin = q1_abs, xmax = dq_abs.
            xmax /= gl.delta();
            dq_rel = xmax;
            xmin = ( xmin - gl.lower() ) / gl.delta() - 0.5;
            // now, xmin = q1_rel, xmax = dq_rel. xmin/max should be min/max( q1_rel, q1_rel - dq_rel )
            sep1_b = - xmin;
            xmin -= (( xmax > 0 ? xmax : 0.0 ) + sf.support / 2.0);
            xmax = xmin + xmax * ( (xmax > 0.0) - ( xmax < 0.0) ) + sf.support;
            ind_b = int( xmin ) + 1 ;
            stride = int( xmax ) + 1 - ind_b;
            sep1_b += ind_b;
          },
          _dq, _I_b, _sep1_b, _stride, q1_abs, dq_abs, grid );

      static_assert( DGrid > 1 && DGrid < 4 );
      if constexpr ( DGrid > 1 ) std::get<1>(_stride) *= std::get<0>(_stride);
      if constexpr ( DGrid > 2 ) std::get<2>(_stride) *= std::get<1>(_stride);
    }


    auto begin() const {
      return sr_iterator( 0, *this );
    }

    auto end() const {
      return sr_iterator( std::get<DGrid-1>(_stride), *this );
    }

  };

}

namespace esirkepov {
  template < typename E1, typename E2, typename Grid,
             typename ShapeF, int DField = 3 >
  inline auto make_shape_range( const apt::VecExpression<E1>& q1_abs,
                                const apt::VecExpression<E2>& dq_abs,
                                const Grid& grid,
                                const ShapeF& shapef ) {
    using T = apt::most_precise_t< apt::element_t<E1>, apt::element_t<E2>, apt::element_t<Grid> >;
    return impl::ShapeRange<T, apt::ndim_v<Grid>, ShapeF, DField>( q1_abs, dq_abs, grid, shapef );
  }
}

namespace particle {

  template < typename Field,
             typename Ptc,
             typename Vec,
             typename ShapeF >
  void depositWJ ( Field& WJ,
                   const PtcExpression<Ptc>& ptc,
                   const apt::VecExpression<Vec>& dq,
                   const ShapeF& shapef ) {
    namespace esir = esirkepov;

    for ( auto[ I, W ] : esir::make_shape_range(ptc.q(), dq, WJ.mesh().bulk(), shapef) ) {
      WJ.template c<0>(I) += std::get<0>(W);
      WJ.template c<1>(I) += std::get<1>(W);

      // FIXME fix the following in DGrid == 2
      // Calling deposition after pusher.calculateDisplacement implies
      // that p_tmp, which is at n+0.5 time step, is based with respect
      // to x^(n+1). However, the x used here is actually x^(n). One way
      // to improve this is obviously calling updatePos before deposition
      // and accordingly change expressions for calculating shapefunctions.
      // FIXME: But, where is J based? Does one really need rebasing momentum?
      constexpr int DGrid = apt::ndim_v<decltype(WJ.mesh().bulk())>;
      if constexpr ( DGrid == 2 ) {
        WJ.template c<2>(I) += std::get<2>(W) * std::get<2>(ptc.p()) / std::sqrt( 1.0 + apt::sqabs(ptc.p()) );
      } else if ( DGrid == 3 ) {
        WJ.template c<2>(I) += std::get<2>(W);
      }
      static_assert( DGrid > 1 && DGrid < 4 );
    }

    return;
  }

}
