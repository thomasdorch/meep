/* Copyright (C) 2003 Massachusetts Institute of Technology
%
%  This program is free software; you can redistribute it and/or modify
%  it under the terms of the GNU General Public License as published by
%  the Free Software Foundation; either version 2, or (at your option)
%  any later version.
%
%  This program is distributed in the hope that it will be useful,
%  but WITHOUT ANY WARRANTY; without even the implied warranty of
%  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
%  GNU General Public License for more details.
%
%  You should have received a copy of the GNU General Public License
%  along with this program; if not, write to the Free Software Foundation,
%  Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <complex>

#include "dactyl.h"
#include "dactyl_internals.h"

polarization *polarization::set_up_polarizations(const mat *ma) {
  if (ma->pb == NULL) return NULL;
  return new polarization(ma->pb);
}

polarization::polarization(const polarizability *the_pb) {
  const volume &v = the_pb->v;
  DOCMP {
    for (int c=0;c<10;c++)
      if (v.has_field((component)c) && is_electric((component)c)) {
        P[c][cmp] = new double[v.ntot()];
        for (int i=0;i<v.ntot();i++) P[c][cmp][i] = 0.0;
        // FIXME perhaps shouldn't allocate the PML split fields if we don't
        // have pml...
        P_pml[c][cmp] = new double[v.ntot()];
        if (P_pml[c][cmp] == NULL) {
          printf("Allocation error in polarization!\n");
          exit(1);
        }
        for (int i=0;i<v.ntot();i++) P_pml[c][cmp][i] = 0.0;
      } else {
        P[c][cmp] = NULL;
        P_pml[c][cmp] = NULL;
      }
  }
  pb = the_pb;
  if (pb->next == NULL) {
    next = NULL;
  } else {
    next = new polarization(pb->next);
  }
}

polarization::~polarization() {
  DOCMP {
    for (int c=0;c<10;c++) delete[] P[c][cmp];
    for (int c=0;c<10;c++) delete[] P_pml[c][cmp];
  }
  if (next) delete next;
}

polarizability::polarizability(const polarizability *pb) {
  omeganot = pb->omeganot;
  gamma = pb->gamma;
  v = pb->v;
  
  sigma = new double[v.ntot()];
  for (int i=0;i<v.ntot();i++) sigma[i] = pb->sigma[i];
  for (int c=0;c<10;c++)
    if (v.has_field((component)c) && is_electric((component)c)) {
      s[c] = new double[v.ntot()];
      for (int i=0;i<v.ntot();i++) s[c][i] = pb->s[c][i];
    } else {
      s[c] = NULL;
    }
  if (pb->next) next = new polarizability(pb->next);
  else next = NULL;
}

void polarizability::use_pml() {
  // Dummy function for now...
  if (next) next->use_pml();
}

polarizability::polarizability(const mat *ma, double sig(const vec &),
                               double om, double ga, double sigscale) {
  v = ma->v;
  omeganot = om;
  gamma = ga;
  next = NULL;

  for (int c=0;c<10;c++)
    if (v.has_field((component)c) && is_electric((component)c)) {
      s[c] = new double[v.ntot()];
    } else {
      s[c] = NULL;
    }
  sigma = new double[v.ntot()];
  if (sigma == NULL) {
    printf("Out of memory in polarizability!\n");
    exit(1);
  }

  if (v.dim == dcyl) {
    for (int i=0;i<v.ntot();i++) sigma[i] = sigscale*sig(v.loc(Hp,i));
  } else if (v.dim == d1) {
    for (int i=0;i<v.ntot();i++) sigma[i] = sigscale*sig(v.loc(Ex,i));
  } else {
    printf("Unsupported dimensionality!\n");
    exit(1);
  }
  // Average out sigma over the grid...
  if (v.dim == dcyl) {
    const vec dr = v.dr()*0.5; // The distance between Yee field components
    const vec dz = v.dz()*0.5; // The distance between Yee field components
    for (int r=1;r<v.nr();r++) {
      const int ir = r*(v.nz()+1);
      const int irm1 = (r-1)*(v.nz()+1);
      for (int z=1;z<=v.nz();z++) {
        s[Er][z + ir] = 0.5*(sigma[z+ir] + sigma[z+ir-1]);
        s[Ep][z + ir] = 0.25*(sigma[z+ir] + sigma[z+ir-1] +
                           sigma[z+irm1] + sigma[z+irm1-1]);
        s[Ez][z + ir] = 0.5*(sigma[z+ir] + sigma[z+irm1]);
      }
    }
    for (int r=0;r<v.nr();r++) {
      const int ir = r*(v.nz()+1);
      const vec here = v.loc(Ep,ir);
      s[Er][ir] = 0.5*sigscale*(sig(here+dr+dz) + sig(here+dr-dz));
      s[Ep][ir] = 0.25*sigscale*(sig(here+dr+dz) + sig(here-dr+dz) +
                              sig(here+dr-dz) + sig(here-dr-dz));
      s[Ez][ir] = 0.5*sigscale*(sig(here+dr+dz) + sig(here-dr+dz));
    }
    for (int z=0;z<v.nz();z++) {
      const vec here = v.loc(Ep,z);
      s[Er][z] = 0.5*sigscale*(sig(here+dr+dz) + sig(here+dr-dz));
      s[Ep][z] = 0.25*sigscale*(sig(here+dr+dz) + sig(here-dr+dz) +
                             sig(here+dr-dz) + sig(here-dr-dz));
      s[Ez][z] = 0.5*sigscale*(sig(here+dr+dz) + sig(here-dr+dz));
    }
  } else if (v.dim == dcyl) {
    // There's just one field point...
  } else {
    printf("Unsupported dimensionality!\n");
    exit(1);
  }
}

polarizability::~polarizability() {
  for (int c=0;c<10;c++) delete[] s[c];
  delete[] sigma;
}

void mat::add_polarizability(double sigma(const vec &),
                             double omega, double gamma, double delta_epsilon) {
  const double freq_conversion = 2*pi*c/a;
  double sigma_scale  = freq_conversion*freq_conversion*omega*omega*delta_epsilon;
  polarizability *npb = new polarizability(this, sigma,
                                           freq_conversion*omega,
                                           freq_conversion*gamma,
                                           sigma_scale);
  npb->next = pb;
  pb = npb;
}

inline double expi(int cmp, double x) {
  return (cmp) ? cos(x) : sin(x);
}

void fields::initialize_polarizations(polarization *op, polarization *np) {
  // Set up polarizations so we'll have them nicely excited, which should
  // give us a handy way of getting all the modes out of a polaritonic
  // material.
  if (op == NULL && np == NULL && olpol != NULL && pol != NULL) {
    initialize_polarizations(olpol, pol);
  } else if (olpol != NULL && pol != NULL) {
    double omt = op->pb->omeganot;
    double amp_shift = exp(op->pb->gamma);
    double sinkz = sin(-omt);
    double coskz = cos(-omt);
    DOCMP {
      for (int c=0;c<10;c++)
        if (v.has_field((component)c) && is_electric((component)c))
          for (int i=0;i<v.ntot();i++) np->P[c][cmp][i] = op->P[c][cmp][i] = f[c][cmp][i];
    }
    if (op->next && np->next) initialize_polarizations(op->next, np->next);
  }
}

void fields::step_polarization_itself(polarization *op, polarization *np) {
  if (op == NULL && np == NULL && olpol != NULL && pol != NULL) {
    // This is the initial call... so I should start running from olpol and pol.
    step_polarization_itself(olpol, pol);
    polarization *temp = olpol;
    olpol = pol;
    pol = temp; // They got switched....
  } else if (olpol != NULL && pol != NULL) {
    const double g = op->pb->gamma;
    const double om = op->pb->omeganot;
    const double funinv = 1.0/(1+0.5*g);

    DOCMP {
      for (int cc=0;cc<10;cc++)
        if (v.has_field((component)cc) && is_electric((component)cc)) {
          for (int i=0;i<v.ntot();i++)
            op->P[cc][cmp][i] = funinv*((2-om*om)*np->P[cc][cmp][i]+
                                        (0.5*g-1)*op->P[cc][cmp][i])+
              np->pb->s[cc][i]*f[cc][cmp][i];
          for (int i=0;i<v.ntot();i++)
            op->P_pml[cc][cmp][i] = funinv*((2-om*om)*np->P_pml[cc][cmp][i]+
                                            (0.5*g-1)*op->P_pml[cc][cmp][i])+
              np->pb->s[cc][i]*f_pml[cc][cmp][i];
        }
    }
    if (op->next && np->next) step_polarization_itself(op->next, np->next);
  }
}

void fields::step_e_polarization(polarization *op, polarization *np) {
  if (op == NULL && np == NULL && olpol != NULL && pol != NULL) {
    // This is the initial call... so I should start running from olpol and pol.
    step_e_polarization(olpol, pol);
  } else if (olpol != NULL && pol != NULL) {
    DOCMP {
      for (int cc=0;cc<10;cc++)
        if (v.has_field((component)cc) && is_electric((component)cc)) {
          for (int i=0;i<v.ntot();i++)
            f[cc][cmp][i] -= ma->inveps[cc][i]*(np->P[cc][i]-op->P[cc][i]);
          for (int i=0;i<v.ntot();i++)
            f_pml[cc][cmp][i] -= ma->inveps[cc][i]*(np->P_pml[cc][i]-op->P_pml[cc][i]);
        }
    }
    if (op->next && np->next) step_e_polarization(op->next, np->next);
  }
}
