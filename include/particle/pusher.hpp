#ifndef _PARTICLEPUSHER_HPP_
#define _PARTICLEPUSHER_HPP_

namespace knl { enum class coordsys_t : unsigned char; }

// TODO check missing 4pi's maybe
namespace particle {
  enum class species : unsigned char;

  template < species sp, typename Ptc, typename T_field, typename T_dp, typename T >
  T_dp update_p( Ptc& ptc, const T& dt, const T_field& E, const T_field& B );

  template < species sp, knl::coordsys_t CS, typename Ptc, typename T_dq, typename T >
  T_dq update_q( Ptc& ptc, const T& dt );
}




#endif
