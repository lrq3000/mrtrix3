/*
    Copyright 2008 Brain Research Institute, Melbourne, Australia

    Written by Robert E. Smith, 31/07/13.

    This file is part of MRtrix.

    MRtrix is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    MRtrix is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with MRtrix.  If not, see <http://www.gnu.org/licenses/>.

*/


#include "image/format/mrtrix_utils.h"


namespace MR
{
  namespace Image
  {
    namespace Format
    {


      void read_mrtrix_header (Header& H, File::KeyValue& kv)
      {

        std::string dtype, layout;
        std::vector<int> dim;
        std::vector<float> transform, dw_scheme, vox, scaling;
        std::vector<std::string> units, labels;

        while (kv.next()) {
          std::string key = lowercase (kv.key());
          if (key == "dim") dim = parse_ints (kv.value());
          else if (key == "vox") vox = parse_floats (kv.value());
          else if (key == "layout") layout = kv.value();
          else if (key == "datatype") dtype = kv.value();
          else if (key == "scaling") scaling = parse_floats (kv.value());
          else if (key == "comments") H.comments().push_back (kv.value());
          else if (key == "units") units = split (kv.value(), "\\");
          else if (key == "labels") labels = split (kv.value(), "\\");
          else if (key == "transform") {
            std::vector<float> V (parse_floats (kv.value()));
            transform.insert (transform.end(), V.begin(), V.end());
          }
          else if (key == "dw_scheme") {
            std::vector<float> V (parse_floats (kv.value()));
            dw_scheme.insert (dw_scheme.end(), V.begin(), V.end());
          }
          else {
            if (H[key].size())
              H[key] += '\n';
            H[key] += kv.value();
          }
        }

        if (dim.empty())
          throw Exception ("missing \"dim\" specification for MRtrix image \"" + H.name() + "\"");
        H.set_ndim (dim.size());
        for (size_t n = 0; n < dim.size(); n++) {
          if (dim[n] < 1)
            throw Exception ("invalid dimensions for MRtrix image \"" + H.name() + "\"");
          H.dim(n) = dim[n];
        }

        if (vox.empty())
          throw Exception ("missing \"vox\" specification for MRtrix image \"" + H.name() + "\"");
        for (size_t n = 0; n < H.ndim(); n++) {
          if (vox[n] < 0.0)
            throw Exception ("invalid voxel size for MRtrix image \"" + H.name() + "\"");
          H.vox(n) = vox[n];
        }


        if (dtype.empty())
          throw Exception ("missing \"datatype\" specification for MRtrix image \"" + H.name() + "\"");
        H.datatype() = DataType::parse (dtype);


        if (layout.empty())
          throw Exception ("missing \"layout\" specification for MRtrix image \"" + H.name() + "\"");
        std::vector<ssize_t> ax = Axis::parse (H.ndim(), layout);
        for (size_t i = 0; i < ax.size(); ++i)
          H.stride(i) = ax[i];

        if (transform.size()) {

          if (transform.size() < 9)
            throw Exception ("invalid \"transform\" specification for MRtrix image \"" + H.name() + "\"");

          H.transform().allocate (4,4);
          int count = 0;
          for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 4; ++col)
              H.transform() (row,col) = transform[count++];
          H.transform()(3,0) = H.transform()(3,1) = H.transform()(3,2) = 0.0;
          H.transform()(3,3) = 1.0;
        }


        if (dw_scheme.size()) {
          if (dw_scheme.size() % 4) {
            INFO ("invalid \"dw_scheme\" specification for MRtrix image \"" + H.name() + "\" - ignored");
          }
          else {
            H.DW_scheme().allocate (dw_scheme.size() /4, 4);
            int count = 0;
            for (size_t row = 0; row < H.DW_scheme().rows(); ++row)
              for (size_t col = 0; col < 4; ++col)
                H.DW_scheme()(row,col) = dw_scheme[count++];
          }
        }


        if (scaling.size()) {
          if (scaling.size() != 2)
            throw Exception ("invalid \"scaling\" specification for MRtrix image \"" + H.name() + "\"");
          H.intensity_offset() = scaling[0];
          H.intensity_scale() = scaling[1];
        }

      }






      void get_mrtrix_file_path (Header& H, const std::string& flag, std::string& fname, size_t& offset)
      {

        Header::iterator i = H.find (flag);
        if (i == H.end())
          throw Exception ("missing \"" + flag + "\" specification for MRtrix image \"" + H.name() + "\"");
        const std::string path = i->second;
        H.erase (i);

        std::istringstream file_stream (path);
        file_stream >> fname;
        offset = 0;
        if (file_stream.good()) {
          try {
            file_stream >> offset;
          }
          catch (...) {
            throw Exception ("invalid offset specified for file \"" + fname
                             + "\" in MRtrix image header \"" + H.name() + "\"");
          }
        }

        if (fname == ".") {
          if (offset == 0)
            throw Exception ("invalid offset specified for embedded MRtrix image \"" + H.name() + "\"");
          fname = H.name();
        } else {
          fname = Path::join (Path::dirname (H.name()), fname);
        }

      }






      void write_mrtrix_header (const Header& H, std::ofstream& out)
      {

        out << "dim: " << H.dim (0);
        for (size_t n = 1; n < H.ndim(); ++n)
          out << "," << H.dim (n);

        out << "\nvox: " << H.vox (0);
        for (size_t n = 1; n < H.ndim(); ++n)
          out << "," << H.vox (n);

        Stride::List stride = Stride::get (H);
        Stride::symbolise (stride);

        out << "\nlayout: " << (stride[0] >0 ? "+" : "-") << abs (stride[0])-1;
        for (size_t n = 1; n < H.ndim(); ++n)
          out << "," << (stride[n] >0 ? "+" : "-") << abs (stride[n])-1;

        out << "\ndatatype: " << H.datatype().specifier();

        for (std::map<std::string, std::string>::const_iterator i = H.begin(); i != H.end(); ++i) {
          std::vector<std::string> lines = split (i->second, "\n", true);
          for (size_t l = 0; l < lines.size(); l++) {
            out << "\n" << i->first << ": " << lines[l];
          }
        }

        for (std::vector<std::string>::const_iterator i = H.comments().begin(); i != H.comments().end(); i++)
          out << "\ncomments: " << *i;


        if (H.transform().is_set()) {
          out << "\ntransform: " << H.transform() (0,0) << "," <<  H.transform() (0,1) << "," << H.transform() (0,2) << "," << H.transform() (0,3);
          out << "\ntransform: " << H.transform() (1,0) << "," <<  H.transform() (1,1) << "," << H.transform() (1,2) << "," << H.transform() (1,3);
          out << "\ntransform: " << H.transform() (2,0) << "," <<  H.transform() (2,1) << "," << H.transform() (2,2) << "," << H.transform() (2,3);
        }

        if (H.intensity_offset() != 0.0 || H.intensity_scale() != 1.0)
          out << "\nscaling: " << H.intensity_offset() << "," << H.intensity_scale();

        if (H.DW_scheme().is_set()) {
          for (size_t i = 0; i < H.DW_scheme().rows(); i++)
            out << "\ndw_scheme: " << H.DW_scheme() (i,0) << "," << H.DW_scheme() (i,1) << "," << H.DW_scheme() (i,2) << "," << H.DW_scheme() (i,3);
        }

        out << "\n";

      }



    }
  }
}