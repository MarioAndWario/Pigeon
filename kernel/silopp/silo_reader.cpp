#include "silopp/silo_reader.hpp"
#include <silo.h>
#include "silopp/silo_datatype.hpp"

namespace silo {
  template < typename file_t >
  bool Reader<file_t>::var_exists( std::string varname ) {
    return DBInqVarExists(_dbfile(), varname.c_str());
  }

  template < typename file_t >
  int Reader<file_t>::var_datatype( std::string varname ) {
    return DBGetVarType(_dbfile(), varname.c_str());
  }

  template < typename file_t >
  std::vector<int> Reader<file_t>::var_dims( std::string varname ) {
    std::vector<int> res(3);
    int ndims = DBGetVarDims(_dbfile(), varname.c_str(), 3, res.data() );
    res.resize(ndims);
    res.shrink_to_fit();
    return res;
  }

  template < typename file_t >
  int Reader<file_t>::var_length( std::string varname ) {
    return DBGetVarLength(_dbfile(), varname.c_str());
  }

  template < typename file_t >
  void Reader<file_t>::read( std::string varname, void* var ) {
    DBReadVar(_dbfile(), varname.c_str(), var );
  }

  template < typename file_t >
  void Reader<file_t>::readslice( std::string varname, const std::vector<apt::array<int,3>>& slice, void* var ) {
    int ndims = slice.size();
    std::vector<int> offset(ndims);
    std::vector<int> length(ndims);
    std::vector<int> stride(ndims);
    for ( int i = 0; i < ndims; ++i ) {
      offset[i] = slice[i][0];
      length[i] = slice[i][1];
      stride[i] = slice[i][2];
    }
    DBReadVarSlice(_dbfile(), varname.c_str(), offset.data(), length.data(), stride.data(), ndims, var );
  }

}

#include "silopp/silo++.hpp"
namespace silo {
  template struct Reader<file_t>;
}
