#ifndef _PARTICLE_ARRAY_HPP_
#define _PARTICLE_ARRAY_HPP_

#include "particle/virtual_particle.hpp"
#include <iterator>
#include <vector>

namespace particle {
  template < typename T, int Dim_Ptc, typename state_t >
  struct array {
  private:
    apt::array<std::vector<T>, Dim_Ptc> _q;
    apt::array<std::vector<T>, Dim_Ptc> _p;
    std::vector<state_t> _state;

  public:
    using value_type = T;
    static constexpr int DPtc = Dim_Ptc;
    using state_type = state_t;

    template < bool isConst >
    class iterator {
    private:
      using array_t = std::conditional_t<isConst, const array, array>;
      array_t& _array;
      int _index;

      // ref_tuple is not default constructible. So recursion is used instead
      template < typename Vec, int I = 0 >
      constexpr auto build_ref_tuple( Vec& x, int i, apt::ref_tuple<decltype(x[0][0]),I> res = {} ) noexcept {
        if constexpr ( I == DPtc ) return res;
        else return build_ref_tuple<Vec,I+1>( x, i, std::tuple_cat( std::move(res), std::tie(x[I][i]) ) );
      }

    public:
      using iterator_category = std::random_access_iterator_tag;
      using difference_type = int;
      using value_type = void;
      using reference = std::conditional_t<isConst, vParticle< const T, Dim_Ptc, const state_t >, vParticle< T, Dim_Ptc, state_t > >;
      using pointer = void;

      iterator( array_t& arr, int i ) noexcept : _array(arr), _index(i) {}

      // TODO check _array is the same?
      inline bool operator== ( const iterator& it ) const noexcept {
        return _index == it._index; }

      inline bool operator!= (const iterator& it) const  noexcept {
        return !(*this == it); }

      // prefix ++ // TODO check bounds
      iterator& operator++ () noexcept { ++_index; return *this; }

      iterator& operator+= ( int n ) noexcept { _index += n; return *this; }

      inline reference operator* () noexcept {
        return reference( build_ref_tuple(_array._q, _index),
                          build_ref_tuple(_array._p, _index),
                          _array._state[_index] );
      }

    };

    friend class iterator<false>;
    friend class iterator<true>;

    inline auto size() const noexcept { return _state.size(); }

    auto begin() noexcept { return iterator<false>( *this, 0 ); }
    auto begin() const noexcept { return iterator<true>( *this, 0 ); }

    auto end() noexcept { return iterator<false>( *this, size() ); }
    auto end() const noexcept { return iterator<true>( *this, size() ); }

    // TODO check performance
    auto operator[] ( int i ) noexcept {
      return *( iterator<false>( *this, i ) );
    }
    auto operator[] ( int i ) const noexcept {
      return *( iterator<true>( *this, i ) );
    }

    template < typename Ptc >
    void push_back( Ptc&& ptc ) {
      auto f = []( auto& arr, auto&& x ) {
                 arr.push_back(std::forward<decltype(x)>(x)); };
      apt::foreach<0,DPtc> ( f, _q, ptc.q() );
      apt::foreach<0,DPtc> ( f, _p, ptc.p() );
      f( _state, ptc.state() );
    }

    // NOTE from is inclusive, to is exclusive. from can be larger than to.
    void erase( unsigned int from, unsigned int to );

    void resize(std::size_t size);
  };



}

namespace std {
  template < typename T, int DPtc, typename state_t >
  class back_insert_iterator<particle::array<T,DPtc, state_t>> {
  private:
    particle::array<T,DPtc,state_t>& _arr;
    int _index;

    using self_type = back_insert_iterator<particle::array<T,DPtc, state_t>>;
  public:
    using iterator_category = std::output_iterator_tag;
    using difference_type = void;
    using value_type = void;
    using reference = void;
    using pointer = void;

    explicit back_insert_iterator( particle::array<T,DPtc, state_t>& arr ) noexcept : _arr(arr) {}

    template < typename Ptc >
    self_type& operator= ( Ptc&& ptc ) {
      _arr.push_back(std::forward<Ptc>(ptc));
      return *this;
    }

    inline self_type& operator++ () noexcept { return *this; }
    inline self_type& operator++ (int) noexcept { return *this; }
    inline self_type& operator* () noexcept { return *this; }

  };

}


#endif
