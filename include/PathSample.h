// Copyright(c) 2020-Present, Matthew R. Hennefarth
// Distributed under the MIT License (http://opensource.org/licenses/MIT)

#ifndef CPET_PATHSAMPLE_H
#define CPET_PATHSAMPLE_H

namespace cpet {
struct PathSample {
  double distance;
  double curvature;

  friend std::ostream& operator<<(std::ostream& os, const PathSample& ps) {
    os << ps.distance << ',' << ps.curvature;
    return os;
  }
};
}

#endif  // CPET_PATHSAMPLE_H

