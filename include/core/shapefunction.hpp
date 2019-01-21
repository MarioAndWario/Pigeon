#ifndef _SHAPEFUNCTION_HPP_
#define _SHAPEFUNCTION_HPP_


namespace sf {
  // value here denotes length of support
  enum class shape : char {
    Nearest_Grid_Point = 1,
    Cloud_In_Cell = 2,
    Triangular_Cloud = 3,
    Piecewise_Cubic_Spline = 4
  };

  constexpr int support( shape s ) noexcept {
    return static_cast<int>(S);
  }

  template < typename T >
  constexpr T radius( shape s ) noexcept {
    return support(s) / 2.0;
  }

  template < shape S >
  struct ShapeFunction {
    template < typename T >
    constexpr T operator() ( T dx ) noexcept {
      dx = std::abs(dx);

      if constexpr( shape::Nearest_Grid_Point == S ) {
          return static_cast<T> ( dx <= 0.5 );
        }
      else if ( shape::Cloud_In_Cell == S ) {
        return std::max ( 1.0 - dx, 0.0 );
      }
      else if ( shape::Triangular_Cloud == S ) {
        return
          static_cast<T>( dx < 0.5 ) * ( 0.75 - dx * dx )
          +
          static_cast<T>( dx >= 0.5 && dx < 1.5 ) * 0.5 * ( 1.5 - dx ) * ( 1.5 - dx );
      }
      else if ( shape::Piecewise_Cubic_Spline == S ) {
        return
          static_cast<T>( dx < 1.0 ) * ( 2.0 / 3.0 - dx * dx * ( 1.0 - 0.5 * dx ) )
          +
          static_cast<T>( dx >= 1.0 && dx < 2.0 ) * ( 2.0 - dx ) * ( 2.0 - dx ) * ( 2.0 - dx )  / 6.0;
      }
    }

  };

}


#endif
