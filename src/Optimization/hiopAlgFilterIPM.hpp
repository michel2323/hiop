// Copyright (c) 2017, Lawrence Livermore National Security, LLC.
// Produced at the Lawrence Livermore National Laboratory (LLNL).
// Written by Cosmin G. Petra, petra1@llnl.gov.
// LLNL-CODE-742473. All rights reserved.
//
// This file is part of HiOp. For details, see https://github.com/LLNL/hiop. HiOp 
// is released under the BSD 3-clause license (https://opensource.org/licenses/BSD-3-Clause). 
// Please also read “Additional BSD Notice” below.
//
// Redistribution and use in source and binary forms, with or without modification, 
// are permitted provided that the following conditions are met:
// i. Redistributions of source code must retain the above copyright notice, this list 
// of conditions and the disclaimer below.
// ii. Redistributions in binary form must reproduce the above copyright notice, 
// this list of conditions and the disclaimer (as noted below) in the documentation and/or 
// other materials provided with the distribution.
// iii. Neither the name of the LLNS/LLNL nor the names of its contributors may be used to 
// endorse or promote products derived from this software without specific prior written 
// permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY 
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES 
// OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT 
// SHALL LAWRENCE LIVERMORE NATIONAL SECURITY, LLC, THE U.S. DEPARTMENT OF ENERGY OR 
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR 
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS 
// OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED 
// AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT 
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, 
// EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Additional BSD Notice
// 1. This notice is required to be provided under our contract with the U.S. Department 
// of Energy (DOE). This work was produced at Lawrence Livermore National Laboratory under 
// Contract No. DE-AC52-07NA27344 with the DOE.
// 2. Neither the United States Government nor Lawrence Livermore National Security, LLC 
// nor any of their employees, makes any warranty, express or implied, or assumes any 
// liability or responsibility for the accuracy, completeness, or usefulness of any 
// information, apparatus, product, or process disclosed, or represents that its use would
// not infringe privately-owned rights.
// 3. Also, reference herein to any specific commercial products, process, or services by 
// trade name, trademark, manufacturer or otherwise does not necessarily constitute or 
// imply its endorsement, recommendation, or favoring by the United States Government or 
// Lawrence Livermore National Security, LLC. The views and opinions of authors expressed 
// herein do not necessarily state or reflect those of the United States Government or 
// Lawrence Livermore National Security, LLC, and shall not be used for advertising or 
// product endorsement purposes.

#ifndef HIOP_ALGFilterIPM
#define HIOP_ALGFilterIPM

#include "hiopNlpFormulation.hpp"
#include "hiopIterate.hpp"
#include "hiopResidual.hpp"
#include "hiopFilter.hpp"
#include "hiopHessianLowRank.hpp"
#include "hiopKKTLinSys.hpp"
#include "hiopLogBarProblem.hpp"
#include "hiopDualsUpdater.hpp"
#include "hiopTimer.hpp"

namespace hiop
{

class hiopAlgFilterIPMBase {
public:
  hiopAlgFilterIPMBase(hiopNlpFormulation* nlp_);
  virtual ~hiopAlgFilterIPMBase();

  /** numerical optimization */
  virtual hiopSolveStatus run() = 0;

  /** computes primal-dual point and returns the evaluation of the problem at this point */
  virtual int startingProcedure(hiopIterate& it_ini,
	       double &f, hiopVector& c_, hiopVector& d_, 
	       hiopVector& grad_,  hiopMatrix& Jac_c,  hiopMatrix& Jac_d);
  /* returns the objective value; valid only after 'run' method has been called */
  double getObjective() const;
  /* returns the primal vector x; valid only after 'run' method has been called */
  void getSolution(double* x) const;
  /* returns the status of the solver */
  /* returns the status of the solver */
  inline hiopSolveStatus getSolveStatus() const { return _solverStatus; }
  /* returns the number of iterations */
  int getNumIterations() const;
protected:
  bool evalNlp(hiopIterate& iter,
	       double &f, hiopVector& c_, hiopVector& d_, 
	       hiopVector& grad_,  hiopMatrix& Jac_c,  hiopMatrix& Jac_d, 
	       hiopMatrix& Hess_L);
  bool evalNlp_funcOnly(hiopIterate& iter, 
			double& f, hiopVector& c_, hiopVector& d_);
  bool evalNlp_derivOnly(hiopIterate& iter, 
			 hiopVector& gradf_,  hiopMatrix& Jac_c,  hiopMatrix& Jac_d,
			  hiopMatrix& Hess_L);
 /* internal helper for error computation */
  virtual bool evalNlpAndLogErrors(const hiopIterate& it, const hiopResidual& resid, const double& mu,
				   double& nlpoptim, double& nlpfeas, double& nlpcomplem, double& nlpoverall,
				   double& logoptim, double& logfeas, double& logcomplem, double& logoverall);
  virtual double thetaLogBarrier(const hiopIterate& it, const hiopResidual& resid, const double& mu);

  bool updateLogBarrierParameters(const hiopIterate& it, const double& mu_curr, const double& tau_curr,
				  double& mu_new, double& tau_new);

  virtual void outputIteration(int lsStatus, int lsNum) = 0;

  //returns whether the algorithm should stop and set an appropriate solve status
  bool checkTermination(const double& _err_nlp, const int& iter_num, hiopSolveStatus& status);
  void displayTerminationMsg();

  void resetSolverStatus();
  virtual void reInitializeNlpObjects();
  virtual void reloadOptions();
private:
  void destructorPart();
protected:
  hiopNlpFormulation* nlp;
  hiopFilter filter;

  hiopLogBarProblem* logbar;

  /* Iterate, search directions (managed by this (algorithm) class) */
  hiopIterate*it_curr;
  hiopIterate*it_trial;
  hiopIterate* dir;

  hiopResidual* resid, *resid_trial;

  int iter_num;
  double _err_nlp_optim, _err_nlp_feas, _err_nlp_complem;//not scaled by sd, sc, and sc
  double _err_nlp_optim0,_err_nlp_feas0,_err_nlp_complem0;//initial errors, not scaled by sd, sc, and sc
  double _err_log_optim, _err_log_feas, _err_log_complem;//not scaled by sd, sc, and sc
  double _err_nlp, _err_log; //max of the above (scaled)

  //class for updating the duals multipliers
  hiopDualsUpdater* dualsUpdate;

  /* Log-barrier problem data 
   *  The algorithm manages these and updates them by calling the   
   *  problem formulation and then adding the contribution from the 
   *  log-barrier term(s). The data that is not iterate dependent,  
   *  such as lower or upper bounds, is in the NlpFormulation       
   */
  double _f_nlp, _f_log, _f_nlp_trial, _f_log_trial;
  hiopVector *_c,*_d, *_c_trial, *_d_trial;
  hiopVector* _grad_f, *_grad_f_trial; //gradient of the log-barrier objective function
  hiopMatrix* _Jac_c, *_Jac_c_trial; //Jacobian of c(x), the equality part
  hiopMatrix* _Jac_d, *_Jac_d_trial; //Jacobian of d(x), the inequality part
  hiopMatrix* _Hess_Lagr;

  /** Algorithms's working quantities */  
  double _mu, _tau, _alpha_primal, _alpha_dual;
  //initialized to 1e4*max{1,\theta(x_0)} and used in the filter as an upper acceptability limit for infeasibility
  double theta_max; 
  //1e-4*max{1,\theta(x_0)} used in the switching condition during the line search
  double theta_min;

  /*** Algorithm's parameters ***/
  double mu0;           //intial mu
  double kappa_mu;      //linear decrease factor in mu 
  double theta_mu;      //exponent for a Mehtrotra-style decrease of mu
  double eps_tol;       //abs tolerance for the NLP error
  double eps_rtol;      //rel tolerance for the NLP error
  double tau_min;       //min value for the fraction-to-the-boundary parameter: tau_k=max{tau_min,1-\mu_k}
  double kappa_eps;     //tolerance for the barrier problem, relative to mu: error<=kappa_eps*mu
  double kappa1,kappa2; //params for default starting point
  double p_smax;        //threshold for the magnitude of the multipliers used in the error estimation
  double gamma_theta,   //sufficient progress parameters for the feasibility violation
    gamma_phi;          //and log barrier objective 
  double s_theta,       //parameters in the switch condition of the linearsearch (eq 19)
    s_phi, delta;
  double eta_phi;       //parameter in the Armijo rule
  double kappa_Sigma;   //parameter in resetting the duals to guarantee closedness of the primal-dual logbar Hessian to the primal logbar Hessian
  int dualsUpdateType;  //type of the update for dual multipliers: 0 LSQ (default, recommended for quasi-Newton); 1 Newton
  int max_n_it;
  int dualsInitializ;  //type of initialization for the duals of constraints: 0 LSQ (default), 1 set to zero
  int accep_n_it;      //after how many iterations with acceptable tolerance should the alg. stop
  double eps_tol_accep;//acceptable tolerance
  //timers
  hiopTimer tmSol;

  //internal flags related to the state of the solver
  hiopSolveStatus _solverStatus;
  int _n_accep_iters;
};

class hiopAlgFilterIPMQuasiNewton : public hiopAlgFilterIPMBase
{
public:
  hiopAlgFilterIPMQuasiNewton(hiopNlpDenseConstraints* nlp);
  virtual ~hiopAlgFilterIPMQuasiNewton();

  virtual hiopSolveStatus run();
private:
  virtual void outputIteration(int lsStatus, int lsNum);
private:
  hiopNlpDenseConstraints* nlpdc;
private:
  hiopAlgFilterIPMQuasiNewton() : hiopAlgFilterIPMBase(NULL) {};
  hiopAlgFilterIPMQuasiNewton(const hiopAlgFilterIPMQuasiNewton& ) : hiopAlgFilterIPMBase(NULL){};
  hiopAlgFilterIPMQuasiNewton& operator=(const hiopAlgFilterIPMQuasiNewton&) {return *this;};
};
//for backward compatibility we make 'hiopAlgFilterIPM' name available
typedef hiopAlgFilterIPMQuasiNewton hiopAlgFilterIPM;



class hiopAlgFilterIPMNewton : public hiopAlgFilterIPMBase
{
public:
  hiopAlgFilterIPMNewton(hiopNlpFormulation* nlp);
  virtual ~hiopAlgFilterIPMNewton();

  virtual hiopSolveStatus run();
private:
  virtual void outputIteration(int lsStatus, int lsNum);
  virtual hiopKKTLinSysCompressed* decideAndCreateLinearSystem(hiopNlpFormulation* nlp);
private:
  hiopAlgFilterIPMNewton() : hiopAlgFilterIPMBase(NULL) {};
  hiopAlgFilterIPMNewton(const hiopAlgFilterIPMNewton& ) : hiopAlgFilterIPMBase(NULL){};
  hiopAlgFilterIPMNewton& operator=(const hiopAlgFilterIPMNewton&) {return *this;};
};

} //end of namespace
#endif
