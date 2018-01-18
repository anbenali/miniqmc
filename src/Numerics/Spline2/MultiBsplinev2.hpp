////////////////////////////////////////////////////////////////////////////////
// This file is distributed under the University of Illinois/NCSA Open Source
// License.  See LICENSE file in top directory for details.
//
// Copyright (c) 2016 Jeongnim Kim and QMCPACK developers.
//
// File developed by:
// Jeongnim Kim, jeongnim.kim@intel.com, Intel Corp.
// Amrita Mathuriya, amrita.mathuriya@intel.com, Intel Corp.
// Ye Luo, yeluo@anl.gov, Argonne National Laboratory
//
// File created by:
// Jeongnim Kim, jeongnim.kim@intel.com, Intel Corp.
////////////////////////////////////////////////////////////////////////////////
// -*- C++ -*-
/**@file MultiBsplinev2.hpp
 *
 * Master header file to define MultiBsplinev2
 *
 * Contains 3D spline evaluation routines.
 */
#ifndef QMCPLUSPLUS_MULTIEINSPLINE_COMMON_HPP
#define QMCPLUSPLUS_MULTIEINSPLINE_COMMON_HPP

#include <iostream>
#include <Numerics/Spline2/MultiBsplineData.hpp>
#include <stdlib.h>

#include <Kokkos_Core.hpp>

#ifdef __CUDA_ARCH__
#undef ASSUME_ALIGNED
#define ASSUME_ALIGNED(s)
#endif

namespace qmcplusplus
{

template <typename T> struct MultiBsplinev2
{

  /// define the einspline object type
  using spliner_type = typename bspline_traits<T, 3>::SplineType;

  MultiBsplinev2() {}
  MultiBsplinev2(const MultiBsplinev2 &in) = delete;
  MultiBsplinev2 &operator=(const MultiBsplinev2 &in) = delete;

  // These things exist because the GPU cannot access the static versions
  T A44[16];
  T dA44[16];
  T d2A44[16];
  //T d3A44[16];

  // This copies the static versions into the local copies for the GPU
  void copy_A44() {
    for (int i=0; i<16; i++) {
      A44[i] = MultiBsplineData<T>::A44[i];
      dA44[i] = MultiBsplineData<T>::dA44[i];
      d2A44[i] = MultiBsplineData<T>::d2A44[i];
      //d3A44[i] = MultiBsplineData<T>::d3A44[i];
    }
  }

  KOKKOS_INLINE_FUNCTION void compute_prefactors(T a[4], T tx) const
  {
    a[0] = ((A44[0] * tx + A44[1]) * tx + A44[2]) * tx + A44[3];
    a[1] = ((A44[4] * tx + A44[5]) * tx + A44[6]) * tx + A44[7];
    a[2] = ((A44[8] * tx + A44[9]) * tx + A44[10]) * tx + A44[11];
    a[3] = ((A44[12] * tx + A44[13]) * tx + A44[14]) * tx + A44[15];
  }

  KOKKOS_INLINE_FUNCTION void compute_prefactors(T a[4], T da[4], T d2a[4], T tx) const
  {
    a[0] = ((A44[0] * tx + A44[1]) * tx + A44[2]) * tx + A44[3];
    a[1] = ((A44[4] * tx + A44[5]) * tx + A44[6]) * tx + A44[7];
    a[2] = ((A44[8] * tx + A44[9]) * tx + A44[10]) * tx + A44[11];
    a[3] = ((A44[12] * tx + A44[13]) * tx + A44[14]) * tx + A44[15];

    da[0] = ((dA44[0] * tx + dA44[1]) * tx + dA44[2]) * tx + dA44[3];
    da[1] = ((dA44[4] * tx + dA44[5]) * tx + dA44[6]) * tx + dA44[7];
    da[2] = ((dA44[8] * tx + dA44[9]) * tx + dA44[10]) * tx + dA44[11];
    da[3] = ((dA44[12] * tx + dA44[13]) * tx + dA44[14]) * tx + dA44[15];

    d2a[0] = ((d2A44[0] * tx + d2A44[1]) * tx + d2A44[2]) * tx + d2A44[3];
    d2a[1] = ((d2A44[4] * tx + d2A44[5]) * tx + d2A44[6]) * tx + d2A44[7];
    d2a[2] = ((d2A44[8] * tx + d2A44[9]) * tx + d2A44[10]) * tx + d2A44[11];
    d2a[3] = ((d2A44[12] * tx + d2A44[13]) * tx + d2A44[14]) * tx + d2A44[15];
  }

#define MYMAX(a,b) (a<b?b:a)
#define MYMIN(a,b) (a<b?b:a)
  KOKKOS_INLINE_FUNCTION void get(T x, T &dx, int &ind, int ng) const
  {
    T ipart;
    dx = std::modf(x, &ipart);
    ind = MYMIN(MYMAX(int(0), static_cast<int>(ipart)), ng);
  }
#undef MYMAX
#undef MYMIN

  /** compute values vals[0,num_splines)
   *
   * The base address for vals, grads and lapl are set by the callers, e.g.,
   * evaluate_vgh(r,psi,grad,hess,ip).
   */
  template<class TeamType>
  KOKKOS_INLINE_FUNCTION
  void evaluate_v(const TeamType& team, const spliner_type *QMC_RESTRICT spline_m, T x, T y, T z, T *QMC_RESTRICT vals, size_t num_splines) const;

  KOKKOS_INLINE_FUNCTION
  void evaluate_vgl(const spliner_type *QMC_RESTRICT spline_m, T x, T y, T z, T *QMC_RESTRICT vals, T *QMC_RESTRICT grads,
                    T *QMC_RESTRICT lapl, size_t num_splines) const;

  template<class TeamType>
  KOKKOS_INLINE_FUNCTION
  void evaluate_vgh_v2(const TeamType& team, const spliner_type *QMC_RESTRICT spline_m, T x, T y, T z, T *QMC_RESTRICT vals, T *QMC_RESTRICT grads,
                    T *QMC_RESTRICT hess, size_t num_splines) const;
};

template <typename T>
template<class TeamType>
KOKKOS_INLINE_FUNCTION
void MultiBsplinev2<T>::evaluate_v(const TeamType& team, const spliner_type *QMC_RESTRICT spline_m,
                                        T x, T y, T z, T *QMC_RESTRICT vals,
                                        size_t num_splines) const
{
  x -= spline_m->x_grid.start;
  y -= spline_m->y_grid.start;
  z -= spline_m->z_grid.start;
  T tx, ty, tz;
  int ix, iy, iz;
  // Make SplineBound and MultiBspineData actual members, or copy functionality over int MultiBspline

  get(x * spline_m->x_grid.delta_inv, tx, ix,
                      spline_m->x_grid.num - 1);
  get(y * spline_m->y_grid.delta_inv, ty, iy,
                      spline_m->y_grid.num - 1);
  get(z * spline_m->z_grid.delta_inv, tz, iz,
                      spline_m->z_grid.num - 1);
  T a[4], b[4], c[4];

  compute_prefactors(a, tx);
  compute_prefactors(b, tx);
  compute_prefactors(c, tx);

  const intptr_t xs = spline_m->x_stride;
  const intptr_t ys = spline_m->y_stride;
  const intptr_t zs = spline_m->z_stride;

  CONSTEXPR T zero(0);
  ASSUME_ALIGNED(vals);

  Kokkos::parallel_for(Kokkos::ThreadVectorRange(team, numsplines),
       [&](const int& i) {
    vals[i] = T();
  });
  
  for (size_t i = 0; i < 4; i++)
    for (size_t j = 0; j < 4; j++)
    {
      const T pre00 = a[i] * b[j];
      const T *QMC_RESTRICT coefs =
          spline_m->coefs + ((ix + i) * xs + (iy + j) * ys + iz * zs);
      ASSUME_ALIGNED(coefs);
      //#pragma omp simd
      Kokkos::parallel_for(Kokkos::ThreadVectorRange(team,num_splines),
			   [&](const int& n) {
        vals[n] +=
            pre00 * (c[0] * coefs[n] + c[1] * coefs[n + zs] +
                     c[2] * coefs[n + 2 * zs] + c[3] * coefs[n + 3 * zs]);
			   });
    }
}

template <typename T>
KOKKOS_INLINE_FUNCTION
void MultiBsplinev2<T>::evaluate_vgl(const spliner_type *QMC_RESTRICT spline_m,
                              T x, T y, T z, T *QMC_RESTRICT vals,
                              T *QMC_RESTRICT grads, T *QMC_RESTRICT lapl,
                              size_t num_splines) const
{
  printf("Error\n");
  /*x -= spline_m->x_grid.start;
  y -= spline_m->y_grid.start;
  z -= spline_m->z_grid.start;
  T tx, ty, tz;
  int ix, iy, iz;
  SplineBound<T>::get(x * spline_m->x_grid.delta_inv, tx, ix,
                      spline_m->x_grid.num - 1);
  SplineBound<T>::get(y * spline_m->y_grid.delta_inv, ty, iy,
                      spline_m->y_grid.num - 1);
  SplineBound<T>::get(z * spline_m->z_grid.delta_inv, tz, iz,
                      spline_m->z_grid.num - 1);

  T a[4], b[4], c[4], da[4], db[4], dc[4], d2a[4], d2b[4], d2c[4];

  MultiBsplineData<T>::compute_prefactors(a, da, d2a, tx);
  MultiBsplineData<T>::compute_prefactors(b, db, d2b, ty);
  MultiBsplineData<T>::compute_prefactors(c, dc, d2c, tz);

  const intptr_t xs = spline_m->x_stride;
  const intptr_t ys = spline_m->y_stride;
  const intptr_t zs = spline_m->z_stride;

  const size_t out_offset = spline_m->num_splines;

  ASSUME_ALIGNED(vals);
  T *QMC_RESTRICT gx = grads;
  ASSUME_ALIGNED(gx);
  T *QMC_RESTRICT gy = grads + out_offset;
  ASSUME_ALIGNED(gy);
  T *QMC_RESTRICT gz = grads + 2 * out_offset;
  ASSUME_ALIGNED(gz);
  T *QMC_RESTRICT lx = lapl;
  ASSUME_ALIGNED(lx);
  T *QMC_RESTRICT ly = lapl + out_offset;
  ASSUME_ALIGNED(ly);
  T *QMC_RESTRICT lz = lapl + 2 * out_offset;
  ASSUME_ALIGNED(lz);

  std::fill(vals, vals + num_splines, T());
  std::fill(gx, gx + num_splines, T());
  std::fill(gy, gy + num_splines, T());
  std::fill(gz, gz + num_splines, T());
  std::fill(lx, lx + num_splines, T());
  std::fill(ly, ly + num_splines, T());
  std::fill(lz, lz + num_splines, T());

  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++)
    {

      const T pre20 = d2a[i] * b[j];
      const T pre10 = da[i] * b[j];
      const T pre00 = a[i] * b[j];
      const T pre11 = da[i] * db[j];
      const T pre01 = a[i] * db[j];
      const T pre02 = a[i] * d2b[j];

      const T *QMC_RESTRICT coefs =
          spline_m->coefs + ((ix + i) * xs + (iy + j) * ys + iz * zs);
      ASSUME_ALIGNED(coefs);
      const T *QMC_RESTRICT coefszs = coefs + zs;
      ASSUME_ALIGNED(coefszs);
      const T *QMC_RESTRICT coefs2zs = coefs + 2 * zs;
      ASSUME_ALIGNED(coefs2zs);
      const T *QMC_RESTRICT coefs3zs = coefs + 3 * zs;
      ASSUME_ALIGNED(coefs3zs);

#pragma noprefetch
#pragma omp simd
      for (int n = 0; n < num_splines; n++)
      {
        const T coefsv    = coefs[n];
        const T coefsvzs  = coefszs[n];
        const T coefsv2zs = coefs2zs[n];
        const T coefsv3zs = coefs3zs[n];

        T sum0 = c[0] * coefsv + c[1] * coefsvzs + c[2] * coefsv2zs +
                 c[3] * coefsv3zs;
        T sum1 = dc[0] * coefsv + dc[1] * coefsvzs + dc[2] * coefsv2zs +
                 dc[3] * coefsv3zs;
        T sum2 = d2c[0] * coefsv + d2c[1] * coefsvzs + d2c[2] * coefsv2zs +
                 d2c[3] * coefsv3zs;
        gx[n] += pre10 * sum0;
        gy[n] += pre01 * sum0;
        gz[n] += pre00 * sum1;
        lx[n] += pre20 * sum0;
        ly[n] += pre02 * sum0;
        lz[n] += pre00 * sum2;
        vals[n] += pre00 * sum0;
      }
    }

  const T dxInv = spline_m->x_grid.delta_inv;
  const T dyInv = spline_m->y_grid.delta_inv;
  const T dzInv = spline_m->z_grid.delta_inv;

  const T dxInv2 = dxInv * dxInv;
  const T dyInv2 = dyInv * dyInv;
  const T dzInv2 = dzInv * dzInv;

#pragma omp simd
  for (int n = 0; n < num_splines; n++)
  {
    gx[n] *= dxInv;
    gy[n] *= dyInv;
    gz[n] *= dzInv;
    lx[n] = lx[n] * dxInv2 + ly[n] * dyInv2 + lz[n] * dzInv2;
  }
  */
}


//Spline Loop interchange (Ye's v2)
//Need to implement a switch to turn this on
template <typename T>
template<class TeamType>
KOKKOS_INLINE_FUNCTION
void MultiBsplinev2<T>::evaluate_vgh(const TeamType& team, const spliner_type *restrict spline_m,
                              T x, T y, T z, T *restrict vals,
                              T *restrict grads, T *restrict hess,
                              size_t num_splines) const
{

  int ix, iy, iz;
  T tx, ty, tz;
  T a[4], b[4], c[4], da[4], db[4], dc[4], d2a[4], d2b[4], d2c[4];

  x -= spline_m->x_grid.start;
  y -= spline_m->y_grid.start;
  z -= spline_m->z_grid.start;
  get(x * spline_m->x_grid.delta_inv, tx, ix,
                      spline_m->x_grid.num - 1);
  get(y * spline_m->y_grid.delta_inv, ty, iy,
                      spline_m->y_grid.num - 1);
  get(z * spline_m->z_grid.delta_inv, tz, iz,
                      spline_m->z_grid.num - 1);

  compute_prefactors(a, da, d2a, tx);
  compute_prefactors(b, db, d2b, ty);
  compute_prefactors(c, dc, d2c, tz);

  const intptr_t xs = spline_m->x_stride;
  const intptr_t ys = spline_m->y_stride;
  const intptr_t zs = spline_m->z_stride;

  const size_t out_offset = spline_m->num_splines;

  ASSUME_ALIGNED(vals);
  T *restrict gx = grads;
  ASSUME_ALIGNED(gx);
  T *restrict gy = grads + out_offset;
  ASSUME_ALIGNED(gy);
  T *restrict gz = grads + 2 * out_offset;
  ASSUME_ALIGNED(gz);

  T *restrict hxx = hess;
  ASSUME_ALIGNED(hxx);
  T *restrict hxy = hess + out_offset;
  ASSUME_ALIGNED(hxy);
  T *restrict hxz = hess + 2 * out_offset;
  ASSUME_ALIGNED(hxz);
  T *restrict hyy = hess + 3 * out_offset;
  ASSUME_ALIGNED(hyy);
  T *restrict hyz = hess + 4 * out_offset;
  ASSUME_ALIGNED(hyz);
  T *restrict hzz = hess + 5 * out_offset;
  ASSUME_ALIGNED(hzz);

  const T dxInv = spline_m->x_grid.delta_inv;
  const T dyInv = spline_m->y_grid.delta_inv;
  const T dzInv = spline_m->z_grid.delta_inv;
  const T dxx   = dxInv * dxInv;
  const T dyy   = dyInv * dyInv;
  const T dzz   = dzInv * dzInv;
  const T dxy   = dxInv * dyInv;
  const T dxz   = dxInv * dzInv;
  const T dyz   = dyInv * dzInv;

  Kokkos::parallel_for(Kokkos::ThreadVectorRange(team,num_splines),
       [&](const int& n) {
    vals[n] = T();
    gx[n] = T();
    gy[n] = T();
    gz[n] = T();
    hxx[n] = T();
    hxy[n] = T();
    hxz[n] = T();
    hyy[n] = T();
    hyz[n] = T();
    hzz[n] = T();

    for (int i = 0; i < 4; i++)
      for (int j = 0; j < 4; j++)
			{
				const T *restrict coefs =
						spline_m->coefs + ((ix + i) * xs + (iy + j) * ys + iz * zs);
				ASSUME_ALIGNED(coefs);
				const T *restrict coefszs = coefs + zs;
				ASSUME_ALIGNED(coefszs);
				const T *restrict coefs2zs = coefs + 2 * zs;
				ASSUME_ALIGNED(coefs2zs);
				const T *restrict coefs3zs = coefs + 3 * zs;
				ASSUME_ALIGNED(coefs3zs);

				const T pre20 = d2a[i] * b[j];
				const T pre10 = da[i] * b[j];
				const T pre00 = a[i] * b[j];
				const T pre11 = da[i] * db[j];
				const T pre01 = a[i] * db[j];
				const T pre02 = a[i] * d2b[j];

        T coefsv    = coefs[n];
        T coefsvzs  = coefszs[n];
        T coefsv2zs = coefs2zs[n];
        T coefsv3zs = coefs3zs[n];

        T sum0 = c[0] * coefsv + c[1] * coefsvzs + c[2] * coefsv2zs +
                 c[3] * coefsv3zs;
        T sum1 = dc[0] * coefsv + dc[1] * coefsvzs + dc[2] * coefsv2zs +
                 dc[3] * coefsv3zs;
        T sum2 = d2c[0] * coefsv + d2c[1] * coefsvzs + d2c[2] * coefsv2zs +
                 d2c[3] * coefsv3zs;

        hxx[n] += pre20 * sum0;
        hxy[n] += pre11 * sum0;
        hxz[n] += pre10 * sum1;
        hyy[n] += pre02 * sum0;
        hyz[n] += pre01 * sum1;
        hzz[n] += pre00 * sum2;
        gx[n] += pre10 * sum0;
        gy[n] += pre01 * sum0;
        gz[n] += pre00 * sum1;
        vals[n] += pre00 * sum0;
      }

    gx[n] *= dxInv;
    gy[n] *= dyInv;
    gz[n] *= dzInv;
    hxx[n] *= dxx;
    hyy[n] *= dyy;
    hzz[n] *= dzz;
    hxy[n] *= dxy;
    hxz[n] *= dxz;
    hyz[n] *= dyz;
  });
}

} /** qmcplusplus namespace */
#endif