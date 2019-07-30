#include "hiopAugLagrSolver.hpp"
#include "hiopAugLagrNlpAdapter.hpp"
#include "hiopNlpFormulation.hpp"
#include "hiopAlgFilterIPM.hpp"

#include <algorithm>    // std::max


namespace hiop
{

hiopAugLagrSolver::hiopAugLagrSolver(NLP_CLASS_IN* nlp_in_) 
  : nlp(new hiopAugLagrNlpAdapter(nlp_in_)),
    n(0),
    m(0),
    _it_curr(nullptr),
    _lam_curr(nullptr),
    _rho_curr(100.),
    residual(nullptr),
    _err_feas0(-1.),
    _err_optim0(-1.),
    _solverStatus(NlpSolve_IncompleteInit),
    iter_num(0),
    _n_accep_iters(0),
    _f_nlp(0.),
    _err_feas(0.), _err_optim(0.),
    eps_tol(1e-6),
    eps_rtol(1e-6),
    eps_tol_accep(1e-4),
    max_n_it(1000),
    accep_n_it(5),
    rho_max(1e7)
{
  // get size of the problem and the penalty term
  long long dum1;
  nlp->get_prob_sizes(n, dum1);
  nlp->get_penalty_size(m);
  
  _it_curr  = new hiopVectorPar(n);
  _lam_curr = new hiopVectorPar(m);
  residual  = new hiopResidualAugLagr(n, m);

  reloadOptions();
  reInitializeNlpObjects();
  resetSolverStatus();
}

hiopAugLagrSolver::~hiopAugLagrSolver() 
{
    if(nlp)       delete nlp;
    if(_it_curr)  delete _it_curr;
    if(_lam_curr) delete _lam_curr;
    if(residual)     delete residual;
}



/**
 */
hiopSolveStatus hiopAugLagrSolver::run()
{

  nlp->log->printf(hovSummary, "==================\nHiop AugLagr SOLVER\n==================\n");

  reloadOptions();
  reInitializeNlpObjects();
  resetSolverStatus();

  _solverStatus = NlpSolve_SolveNotCalled;
  
  nlp->runStats.initialize();
  nlp->runStats.tmOptimizTotal.start();
  nlp->runStats.tmStartingPoint.start();

  //initialize curr_iter by calling TNLP starting point + do something about slacks
  // and set starting point at the Adapter class for the first major AL iteration
  nlp->get_user_starting_point(n, _it_curr->local_data());
  nlp->set_starting_point(n, _it_curr->local_data_const());

  //set initial guess of the multipliers and the penalty parameter
  //TODO hot start for lambda
  _lam_curr->setToConstant(1.); nlp->set_lambda(_lam_curr);
  _rho_curr = 100.;             nlp->set_rho(_rho_curr);
  
  nlp->runStats.tmStartingPoint.stop();
  
  //initial evaluation of the problem
  iter_num=0; nlp->runStats.nIter=iter_num;

  //evaluate the problem
  evalNlp(_it_curr, _f_nlp);
  evalNlpErrors(_it_curr, residual, _err_feas, _err_optim);
  nlp->log->write("First residual-------------", *residual, hovIteration);

  //check termination conditions   
  bool notConverged = true;
  if(checkTermination(_err_feas, _err_optim, iter_num, _solverStatus)) {
      notConverged = false;
    }

  //remember the initial error for rel. tol. test
  _err_feas0 = _err_feas;
  _err_optim0 = _err_optim;


  ////////////////////////////////////////////////////////////////////////////////////
  // run baby run
  ////////////////////////////////////////////////////////////////////////////////////

  // --- Algorithm status 'algStatus ----
  //-1 couldn't solve the problem (most likely because small search step. Restauration phase likely needed)
  // 0 stopped due to tolerances, including acceptable tolerance, or relative tolerance
  // 1 max iter reached
  // 2 user stop via the iteration callback

  //int algStatus=0;
  bool bret=true;
  _solverStatus = NlpSolve_Pending;
  while(notConverged) {


    nlp->log->printf(hovScalars, "  Nlp     errs: infeas:%20.14e   optimality:%20.14e\n", _err_feas, _err_optim);
    outputIteration();

    //TODO: this signature does not make sense for the AL
    //user callback
    //if(!nlp->user_callback_iterate(iter_num, _f_nlp,
	//			   *_it_curr->get_x(),
	//			   *_it_curr->get_zl(),
	//			   *_it_curr->get_zu(),
	//			   *_penaltyFcn,*_d,
	//			   *_it_curr->get_yc(),  *_it_curr->get_yd(), //lambda,
	//			   _err_nlp_feas, _err_nlp_optim,
	//			   _mu,
	//			   _alpha_dual, _alpha_primal,  lsNum)) {
    //  _solverStatus = User_Stopped; break;
    //}

    
    /****************************************************
     * Solve the AL subproblem by calling HiOP or Ipopt
     ***************************************************/
    nlp->runStats.tmSolverInternal.start();  
    hiopNlpDenseConstraints subproblem(*nlp);

    subproblem.options->SetStringValue("fixed_var", "relax"); //remove fails
    subproblem.options->SetIntegerValue("verbosity_level", 0);
    //subproblem.options->SetNumericValue("tolerance", 1e-4);
    //subproblem.options->SetStringValue("dualsInitialization",  "zero");
    //subproblem.options->SetIntegerValue("max_iter", 2);

    hiopAlgFilterIPM solver(&subproblem);
    hiopSolveStatus status = solver.run();

    nlp->runStats.tmSolverInternal.stop();
    //solver.getObjective(); //AL fcn, not user objective
    
    // update the current iterate, used as x0 for the next subproblem
    solver.getSolution(_it_curr->local_data());
    
    
    nlp->log->printf(hovIteration, "Iter[%d] -> full iterate:", iter_num);
    nlp->log->write("", *_it_curr, hovIteration);
    nlp->log->write("", *_lam_curr, hovIteration);

    /*************************************************
     * Error evalutaion & Termination check
     ************************************************/
    //evaluate the problem and NLP errors
    //TODO: probably doesn't need to re-evaluate Ipopt's fcns
    evalNlp(_it_curr, _f_nlp);
    evalNlpErrors(_it_curr, residual, _err_feas, _err_optim);
   
    nlp->log->printf(hovIteration, "Iter[%d] full residual:-------------\n", iter_num);
    nlp->log->write("", *residual, hovIteration);
  
    //check termination conditions   
    if(checkTermination(_err_feas, _err_optim, iter_num, _solverStatus)) {
        break;
      }

    /************************************************
     * set starting point for the next major iteration
     ************************************************/
    nlp->set_starting_point(n, _it_curr->local_data_const());

    /************************************************
     * update rho and lambdas
     ************************************************/
    updateLambda();
    updateRho();

    iter_num++; nlp->runStats.nIter=iter_num;
  }

  nlp->runStats.tmOptimizTotal.stop();

  //_solverStatus contains the termination information
  //TODO: displayTerminationMsg();

  //TODO: user callback signature does not make sense
  //user callback
  //nlp->user_callback_solution(_solverStatus,
  //  		      *_it_curr->get_x(),
  //  		      *_it_curr->get_zl(),
  //  		      *_it_curr->get_zu(),
  //  		      *_penaltyFcn,*_d,
  //  		      *_it_curr->get_yc(),  *_it_curr->get_yd(),
  //  		      _f_nlp);

  return _solverStatus;
}

//TODO: can be merged with evalNlpErrors()
bool hiopAugLagrSolver::evalNlp(hiopVectorPar* iter,                              
                               double &f/**, hiopVector& c_, hiopVector& d_, 
                               hiopVector& gradf_,  hiopMatrixDense& Jac_c,  hiopMatrixDense& Jac_d*/)
{
  bool new_x=true, bret; 
  double* x = iter->local_data();//local_data_const();
  //hiopVectorPar 
  //  &c=dynamic_cast<hiopVectorPar&>(c_), 
  //  &d=dynamic_cast<hiopVectorPar&>(d_), 
  //  &gradf=dynamic_cast<hiopVectorPar&>(gradf_);
  
  //TODO: do we want the original objective, not the AL!!
  bret = nlp->eval_f_user(n, x, new_x, f); assert(bret);
  new_x= false; //same x for the rest
  //bret = nlp->eval_grad_f(x, new_x, gradf.local_data());  assert(bret);

  //bret = nlp->eval_c     (x, new_x, c.local_data());     assert(bret);
  //bret = nlp->eval_d     (x, new_x, d.local_data());     assert(bret);
  //bret = nlp->eval_Jac_c (x, new_x, Jac_c.local_data()); assert(bret);
  //bret = nlp->eval_Jac_d (x, new_x, Jac_d.local_data()); assert(bret);

  return bret;
}


/**
 * Evaluates errors  of the Augmented lagrangian, namely
 * the feasibility error represented by the penalty function p(x,s)
 * and the optimality error represented by gradient of the Lagrangian
 * d_L = d_f(x) + J(x)^T lam
 *
 * @param[in] current_iterate The latest iterate in (x,s)
 * @param[out] resid Residual class keeping information about the NLP errors
 * @param[out} err_optim, err_feas Optimality and feasibility errors
 */
bool hiopAugLagrSolver::evalNlpErrors(const hiopVector *current_iterate, 
                                      hiopResidualAugLagr *resid, double& err_feas, double& err_optim)
{
  double *penaltyFcn = resid->getFeasibilityPtr();
  double *gradLagr = resid->getOptimalityPtr();
  const double *_it_curr_data = _it_curr->local_data_const();
  bool new_x = true;

  //evaluate the Adapter penalty fcn and gradient of the Lagrangian
  bool bret = nlp->eval_residuals(n, _it_curr_data, new_x, penaltyFcn, gradLagr);
  assert(bret);

  //recompute the residuals norms
  resid->update();
  
  //actual nlp errors 
  err_feas  = resid->getFeasibilityNorm();
  err_optim = resid->getOptimalityNorm();

  return bret;
}


/**
 * Test checking for stopping the augmented Lagrangian loop given the NLP errors in @resid, number of iterations @iter_num. Sets the status if appropriate.
 */
bool hiopAugLagrSolver::checkTermination(double err_feas, double err_optim, const int iter_num, hiopSolveStatus &status)
{
  if (err_feas<=eps_tol && err_optim<=eps_tol)
  {
      status = Solve_Success;
      return true;
  }
  
  if (iter_num>=max_n_it)
  {
      status = Max_Iter_Exceeded;
      return true;
  }

  if(eps_rtol>0) {
    if(err_optim   <= eps_rtol * _err_optim0 &&
       err_feas    <= eps_rtol * _err_feas0)
    {
      status = Solve_Success_RelTol;
      return true;
    }
  }

  if (err_feas<=eps_tol_accep && err_optim<=eps_tol_accep) _n_accep_iters++;
  else _n_accep_iters = 0;

  if(_n_accep_iters>=accep_n_it) { status = Solve_Acceptable_Level; return true; }

  return false;
}

void hiopAugLagrSolver::outputIteration()
{
  if(iter_num/10*10==iter_num) 
    nlp->log->printf(hovSummary, "iter    objective     inf_pr     inf_du   lg(rho)\n");

    nlp->log->printf(hovSummary, "%4d %14.7e %7.3e  %7.3e %6.2f\n",
                     iter_num, _f_nlp, _err_feas, _err_optim, log10(_rho_curr)); 
}

/**
 * Computes new value of the lagrange multipliers estimate
 * lam_k+1 = lam_k + penaltyFcn_k/rho_k
 */
void hiopAugLagrSolver::updateLambda()
{
    double *_lam_data = _lam_curr->local_data();
    const double *penaltyFcn = residual->getFeasibilityPtr();

    // compute new value of the multipliers
    for (long long i=0; i<m; i++)
        _lam_data[i] += penaltyFcn[i]/_rho_curr;

    //update the multipliers in the adapter class
    nlp->set_lambda(_lam_curr);
}


/**
 * Computes new value of the penalty parameter
 */
void hiopAugLagrSolver::updateRho()
{
    //compute new value of the penalty parameter
    _rho_curr = std::max(10*_rho_curr, rho_max); //TODO

    //update the penalty parameter in the adapter class
    nlp->set_rho(_rho_curr);
}

void hiopAugLagrSolver::reInitializeNlpObjects()
{
}

void hiopAugLagrSolver::reloadOptions()
{
  //algorithm parameters parameters
  eps_tol  = nlp->options->GetNumeric("tolerance");        //absolute error for the nlp
  eps_rtol = nlp->options->GetNumeric("rel_tolerance");    //relative error (to errors for the initial point)
  eps_tol_accep = nlp->options->GetNumeric("acceptable_tolerance");

  max_n_it  = nlp->options->GetInteger("max_iter");
  accep_n_it    = nlp->options->GetInteger("acceptable_iterations");
}

void hiopAugLagrSolver::resetSolverStatus()
{
  _n_accep_iters = 0;
  _solverStatus = NlpSolve_IncompleteInit;
}

/* returns the objective value; valid only after 'run' method has been called */
double hiopAugLagrSolver::getObjective() const
{
//  if(_solverStatus==NlpSolve_IncompleteInit || _solverStatus == NlpSolve_SolveNotCalled)
//    nlp->log->printf(hovError, "getObjective: hiOp did not initialize entirely or the 'run' function was not called.");
//  if(_solverStatus==NlpSolve_Pending)
//    nlp->log->printf(hovWarning, "getObjective: hiOp does not seem to have completed yet. The objective value returned may not be optimal.");
//  return nlp->user_obj(_f_nlp);
  return 0.0; //TODO
}

/* returns the primal vector x; valid only after 'run' method has been called */
void hiopAugLagrSolver::getSolution(double* x) const
{
//  if(_solverStatus==NlpSolve_IncompleteInit || _solverStatus == NlpSolve_SolveNotCalled)
//    nlp->log->printf(hovError, "getSolution: hiOp did not initialize entirely or the 'run' function was not called.");
//  if(_solverStatus==NlpSolve_Pending)
//    nlp->log->printf(hovWarning, "getSolution: hiOp have not completed yet. The primal vector returned may not be optimal.");
//
//  hiopVectorPar& it_x = dynamic_cast<hiopVectorPar&>(*it_curr->get_x());
//  //it_curr->get_x()->copyTo(x);
//  nlp->user_x(it_x, x);
//TODO
}

/* returns the status of the solver */
hiopSolveStatus hiopAugLagrSolver::getSolveStatus() const
{
  return _solverStatus;
}

/* returns the number of iterations */
int hiopAugLagrSolver::getNumIterations() const
{
//TODO
//  if(_solverStatus==NlpSolve_IncompleteInit || _solverStatus == NlpSolve_SolveNotCalled)
//    nlp->log->printf(hovError, "getNumIterations: hiOp did not initialize entirely or the 'run' function was not called.");
//  if(_solverStatus==NlpSolve_Pending)
//    nlp->log->printf(hovWarning, "getNumIterations: hiOp does not seem to have completed yet. The objective value returned may not be optimal.");
  return nlp->runStats.nIter;
}
}
