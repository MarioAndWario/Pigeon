#include "parallel/mpi_collective.hpp"
#include "parallel/mpi++.hpp"

namespace mpi::impl {
  template < typename T >
  struct is_inter {
    constexpr static bool value = false;
  };

  template <>
  struct is_inter<InterComm> {
    constexpr static bool value = true;
  };

  template < typename T >
  inline constexpr bool is_inter_v = is_inter<T>::value;
}

namespace mpi {
  template < typename Comm >
  void Collective_Comm<Comm>::barrier() const {
    MPI_Barrier( _comm() );
  }

  constexpr auto mpi_op( by op ) {
    if ( by::SUM == op ) return MPI_SUM;
    if ( by::MAX == op ) return MPI_MAX;
    if ( by::MAXLOC == op ) return MPI_MAXLOC;
  }

  template < typename Comm >
  template < bool In_Place, typename T >
  std::optional<std::vector<T>>
  Collective_Comm<Comm>::reduce( by op, int root, T* buffer, int count ) const {
    std::optional<std::vector<T>> result;

    const void* send_buf = nullptr;
    void* recv_buf = nullptr;

    if constexpr (impl::is_inter_v<Comm>) {
        if ( MPI_ROOT == root ) recv_buf = buffer;
        else if ( MPI_PROC_NULL == root );
        else send_buf = buffer;
    } else {
      int myrank{};
      MPI_Comm_rank(_comm(), &myrank);
      if ( myrank != root ) {
        send_buf = buffer;
      } else {
        if constexpr( In_Place ) {
            send_buf = MPI_IN_PLACE;
            recv_buf = buffer;
          } else {
          send_buf = buffer;
          result.emplace(); // copy construct the recv_buf
          (*result).reserve(count);
          (*result).resize(count);
          recv_buf = (*result).data();
        }
      }
    }


    MPI_Reduce( send_buf, recv_buf, count, datatype<T>(), mpi_op(op), root, _comm() );

    return result;
  }

  // template < typename Comm >
  // template < by Op, bool In_Place, typename T >
  // std::tuple< Request, std::optional<std::vector<T> > >
  // Collective_Comm<Comm,Inter>::Ireduce( T& buffer, int root ) const {
  //   static_assert( !Inter );
  //   std::optional<std::vector<T>> result;

  //   const void* send_buf = nullptr;
  //   void* recv_buf = nullptr;

  //   if ( _comm().rank() != root ) {
  //     send_buf = buf;
  //   } else {
  //     if constexpr( In_Place ) {
  //         send_buf = MPI_IN_PLACE;
  //         recv_buf = buffer;
  //       } else {
  //       send_buf = buffer;
  //       result.emplace(); // copy construct the recv_buf
  //       (*result).resize(count);
  //       (*result).shrink_to_fit();
  //       recv_buf = (*result).data();
  //     }
  //   }

  //   Request req;
  //   MPI_Ireduce( send_buf, recv_buf, count, datatype, mpi_op(op), root, _comm(), req );

  //   return std::make_tuple( req, result );
  // }

  template < typename Comm >
  template < typename T >
  void Collective_Comm<Comm>::broadcast( int root, T* buffer, int count ) const {
    MPI_Bcast( buffer, count, datatype<T>(), root, _comm() );
  }


  template < typename Comm >
  template < typename T >
  Request Collective_Comm<Comm>::Ibroadcast( int root, T* buffer, int count ) const {
    Request req;
    MPI_Ibcast( buffer, count, datatype<T>(), root, _comm(), req );
    return req;
  }


  template < typename Comm >
  template < typename T >
  std::vector<T> Collective_Comm<Comm>::allgather( const T* send_buf, int send_count ) const {
    int size{};
    MPI_Comm_size(_comm(), &size);
    std::vector<T> recv( send_count * size ); // CLAIM: also works with intercomm
    recv.shrink_to_fit();
    MPI_Allgather( send_buf, send_count, datatype<T>(), recv.data(), recv.size(), datatype<T>(), _comm() );
    return recv;
  }

  template < typename Comm >
  template < typename T >
  void Collective_Comm<Comm>::scatter( int root, T* buffer, int count ) const {
    void* sendbuf = nullptr;
    void* recvbuf = nullptr;
    int sendcount = 0, recvcount = 0;
    if constexpr ( impl::is_inter_v<Comm> ) {
        if ( MPI_ROOT == root ) {
          sendbuf = buffer;
          sendcount = count;
        }
        else if ( MPI_PROC_NULL == root );
        else {
          recvbuf = buffer;
          recvcount = count;
        }
      }
    else {
      int myrank{};
      MPI_Comm_rank(_comm(), &myrank);
      if ( myrank == root ) {
        sendbuf = buffer;
        sendcount = count;
        recvbuf = MPI_IN_PLACE;
      } else {
        recvbuf = buffer;
        recvcount = count;
      }
    }

    MPI_Scatter(sendbuf, sendcount, datatype<T>(), recvbuf, recvcount, datatype<T>(), root, _comm() );
  }
}

// instantiation
namespace mpi {
#define INSTANTIATE_MPI_COLLECTIVE_FOR_COMM(_COMM_, _TYPE_)             \
  template std::optional<std::vector<_TYPE_>>                           \
  Collective_Comm<_COMM_>::reduce<false>( by, int, _TYPE_ *, int ) const; \
  template std::optional<std::vector<_TYPE_>>                           \
  Collective_Comm<_COMM_>::reduce<true>( by, int, _TYPE_ *, int ) const; \
  template void Collective_Comm<_COMM_>::broadcast( int, _TYPE_ *, int ) const; \
  template Request Collective_Comm<_COMM_>::Ibroadcast( int, _TYPE_ *, int ) const; \
  template std::vector<_TYPE_> Collective_Comm<_COMM_>::allgather( const _TYPE_*, int ) const; \
  template void Collective_Comm<_COMM_>::scatter( int, _TYPE_*, int ) const

#define INSTANTIATE_MPI_COLLECTIVE(_TYPE_)                \
  INSTANTIATE_MPI_COLLECTIVE_FOR_COMM(Comm, _TYPE_);      \
  INSTANTIATE_MPI_COLLECTIVE_FOR_COMM(InterComm, _TYPE_)

  INSTANTIATE_MPI_COLLECTIVE(char);
  INSTANTIATE_MPI_COLLECTIVE(int);
  INSTANTIATE_MPI_COLLECTIVE(unsigned int);
  INSTANTIATE_MPI_COLLECTIVE(float);
  INSTANTIATE_MPI_COLLECTIVE(double);
  INSTANTIATE_MPI_COLLECTIVE(long double);
}

#include "mpi_cparticle.cpp"
namespace mpi {
  INSTANTIATE_MPI_COLLECTIVE(cPtc_t);
}
