#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Created on Tue Feb 23 2021

@author: ajafari

NOTE:
Execute this script from the root directory of the repository:
        ../../fenics-constitutive/
    via the command:
        python examples/example_plasticity.py
"""

import os, sys
from pathlib import Path
sys.path.insert(0, '..')
sys.path.insert(0, '.')

from examples.helper import *
from examples.helper_plastic_problem import *
from examples.fenics_plastic_constitutive import *
from examples.fenics_plastic_problem import *
_format = '.png'
sz = 14

class FenicsConfig:
    def __init__(self):
        # interpolation degree of displacement field
        self.deg_d = 3        
        # quadrature degree
        self.deg_q = 4
        # element family
        self.element_family = 'CG'

class ParsCantileverBeam:
    def __init__(self):
        self.constraint = 'PLANE_STRESS'
        # self.constraint = '3D'
        
        self.E = 1000.
        self.nu = 0.3
        self.lx = 6.0
        self.ly = 0.5
        self.lz = 0.5 # only relevant for 3D case or ploting reaction force in 2D case
        
        self.unit_res = 6 # the number of mesh per unit length of geometry (can be float)
        
        # sinusoidal loading (at the beam tip)
        _level = 4 * self.ly
        _scales = np.array([1])
        _N = 1.5
        self.loading = sinusoidal_loading(_vals=_level * _scales, N=_N)
        
        self.sol_res = int(_N * 83)
            
        # body force (zero)
        if self.constraint=='PLANE_STRESS' or self.constraint=='PLANE_STRAIN':
            self.geo_dim = 2
        elif self.constraint=='3D':
            self.geo_dim = 3
        self.f = df.Constant(self.geo_dim * [0.0])
        
        # others
        self._plot = True
        self._write_files = True

class ParsCantileverBeamPlastic(ParsCantileverBeam):
    def __init__(self):
        super().__init__()
        
        self.sig0 = 12.0 # yield strength
        
        ### NO Hardening
        # self.H = 0.0 # Hardening modulus
        
        ### ISOTROPIC Hardening
        Et = self.E / 100.0
        self.H = 15 * self.E * Et / (self.E - Et) # Hardening modulus
        ## Hardening hypothesis
        self.hardening_hypothesis = 'unit' # denoting the type of the harding function "P(sigma, kappa)"
        # self.hardening_hypothesis = 'plastic-work'

class CantileverBeamPlasticModel:
    def __init__(self, pars, _name=None):
        self.pars = pars
        self.prm = FenicsConfig()
        if _name is None:
            _name = 'CantBeamPlastic_deg' + str(self.prm.deg_d) + '_' + str(self.pars.constraint) + '_H=' \
                + '%.1f'%self.pars.H
            if self.pars.H != 0:
                _name  += '_P=' + self.pars.hardening_hypothesis
        self._name = _name
        self._set_path()
        self._establish_model()
        
    def _set_path(self):
        self._path = str(Path(__file__).parent) + '/' + self._name + '/'
        make_path(self._path)
    
    def _establish_model(self):
        ### MESH ###
        res_x = int(self.pars.unit_res * self.pars.lx)
        res_y = int(self.pars.unit_res * self.pars.ly)
        if self.pars.geo_dim==2:
            mesh = df.RectangleMesh(df.Point(0,0), df.Point(self.pars.lx, self.pars.ly), res_x, res_y)
        elif self.pars.geo_dim==3:
            res_z = int(self.pars.unit_res * self.pars.lz)
            mesh = df.BoxMesh(df.Point(0.0, 0.0, 0.0), df.Point(self.pars.lx, self.pars.ly, self.pars.lz) \
                              , res_x, res_y, res_z)
        
        ### MATERIAL ###
        yf = Yield_VM(self.pars.sig0, constraint=self.pars.constraint, H=self.pars.H)
        if self.pars.H == 0: ## perfect plasticity (No hardening)
            mat = PlasticConsitutivePerfect(self.pars.E, nu=self.pars.nu \
                                            , constraint=self.pars.constraint, yf=yf)
        else: ## Isotropic-hardenning plasticity
            if self.pars.hardening_hypothesis == 'unit':
                ri = RateIndependentHistory() # p = 1, i.e.: kappa_dot = lamda_dot
            elif self.pars.hardening_hypothesis == 'plastic-work':
                ri = RateIndependentHistory_PlasticWork(yf) # kappa_dot = plastic work rate
            mat = PlasticConsitutiveRateIndependentHistory(self.pars.E, nu=self.pars.nu \
                                                           , constraint=self.pars.constraint, yf=yf, ri=ri)
        
        #### PROBLEM ###
        self.fen = PlasticProblem(mat, mesh, prm=self.prm, constraint=self.pars.constraint)
        
        #### BCs and LOADs ###
        if self.pars.geo_dim == 2:
            self.fen.bcs_DR \
                , self.fen.bcs_DR_dofs \
                    , self.fen.bcs_DR_inhom \
                        , self.fen.bcs_DR_inhom_dofs \
            = load_and_bcs_on_cantileverBeam2d(mesh, self.pars.lx, self.pars.ly, self.fen.V, self.pars.loading)
        elif self.pars.geo_dim == 3:
            self.fen.bcs_DR \
                , self.fen.bcs_DR_dofs \
                    , self.fen.bcs_DR_inhom \
                        , self.fen.bcs_DR_inhom_dofs \
            = load_and_bcs_on_cantileverBeam3d(mesh, self.pars.lx, self.pars.ly, self.pars.lz, self.fen.V, self.pars.loading)
        
        #### TIME-DEPENDENCIes
        self.fen.add_time_varying_loadings(self.pars.loading)
        
        #### BUILD SOLVER ###
        self.fen.set_bcs(self.fen.bcs_DR+self.fen.bcs_DR_inhom)
    
    def solve(self, t_end, checkpoints=[], t_start=0, dt=None, reaction_dofs=None):
        # post-processor
        pp = PostProcessPlastic(self.fen, self._name, self._path, reaction_dofs=reaction_dofs, write_files=self.pars._write_files)
        
        # time-stepper
        ts = TimeStepper(self.fen.solve_t, pp, u=self.fen.u)
        
        # solve
        if dt==None:
            dt = t_end / self.pars.sol_res
        converged = ts.equidistant(t_end, dt=dt)
        
        if self.pars._plot:
            if reaction_dofs is not None:
                _tit = 'Reaction force at top-left node, ' + str(self.pars.constraint) + ', H=' + '%.1f'%self.pars.H
                if self.pars.H != 0:
                    _tit  += ', P=' + self.pars.hardening_hypothesis
                
                if self.pars.geo_dim == 2:
                    _factor = self.pars.lz
                elif self.pars.geo_dim == 3:
                    _factor = 1.0
                pp.plot_reaction_forces(_tit, full_file_name=self._path + self._name + '_reaction_force' + _format, factor=_factor)
                
                ## together with the linear tangent stiffness and possible reference MATLAB solution
                _, u_sol = evaluate_expression_of_t(self.pars.loading, t0=0.0, t_end=t_end, _res=self.pars.sol_res)
                plt.figure()
                if self.pars.geo_dim == 2:
                    f_sol = self.pars.lz * np.array(pp.reaction_forces)
                elif self.pars.geo_dim == 3:
                    f_sol = [sum(f) for f in pp.reaction_forces]
                plt.plot(u_sol, f_sol, marker='.', label='FEniCS: Plasticity')
                K_analytical = self.pars.lz * (self.pars.ly ** 3) * self.pars.E / 4 / (self.pars.lx ** 3)
                u_analytical = [u for u in u_sol if (K_analytical*u<=max(f_sol) and K_analytical*u>=min(f_sol))]
                f_analytical = K_analytical * np.array(u_analytical)
                plt.plot(u_analytical, f_analytical, label='Equivalent elastic stiffness')
                plt.title(_tit, fontsize=sz)
                plt.xlabel('u', fontsize=sz)
                plt.ylabel('f', fontsize=sz)
                plt.xticks(fontsize=sz)
                plt.yticks(fontsize=sz)
                plt.legend()
                plt.savefig(self._path + self._name + '_reaction_force_compare' + _format)
                plt.show()
                plt.show()
        
        print('One FEniCS problem of "' + self._name + '" was solved.')
        return pp, [[u_analytical, u_sol], [f_analytical, f_sol], ['Equivalent elastic stiffness', 'FEniCS: Plasticity']]
    
if __name__ == "__main__":
    df.set_log_level(30)
    
    pars = ParsCantileverBeamPlastic()
    
    model = CantileverBeamPlasticModel(pars)
    checkpoints = []
    reaction_dofs = model.fen.bcs_DR_inhom_dofs
        
    pp, plots_data = model.solve(t_end=1.0, reaction_dofs=reaction_dofs, checkpoints=checkpoints)
    
    ## print out minimum and maximum reaction forces
    msg = f"F_min, F_max = {min([sum(ri) for ri in pp.reaction_forces])}, {max([sum(ri) for ri in pp.reaction_forces])} ."
    print(msg)
    
    df.parameters["form_compiler"]["representation"] = "uflacs" # back to default setup of dolfin