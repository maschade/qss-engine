/*****************************************************************************

 This file is part of QSS Solver.

 QSS Solver is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 QSS Solver is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with QSS Solver.  If not, see <http://www.gnu.org/licenses/>.

 ******************************************************************************/

/* Header files with a description of contents used */

#include <cvode/cvode.h>             /* prototypes for CVODE fcts., consts. */
#include <nvector/nvector_serial.h>  /* serial N_Vector types, fcts., macros */
#include <cvode/cvode_dense.h>       /* prototype for CVDense */
#include <sundials/sundials_dense.h> /* definitions DlsMat DENSE_ELEM */
#include <sundials/sundials_types.h> /* definition of type realtype */

#define USE_JACOBIAN
#ifdef USE_JACOBIAN
#include <cvode/cvode_superlumt.h>   /* prototype for CVSUPERLUMT */
#include <sundials/sundials_sparse.h> /* definitions SlsMat */
#endif


#include <classic/classic_cvode_integrator.h>

#include <stdio.h>
#include <stdlib.h>

#include "../common/data.h"
#include "../common/simulator.h"
#include "../common/utils.h"
#include "classic_data.h"
#include "classic_integrator.h"
#include "classic_simulator.h"

#define Ith(v,i)    NV_Ith_S(v,i)       /* Ith numbers components 1..NEQ */
#define IJth(A,i,j) DENSE_ELEM(A,i,j) /* IJth numbers rows,cols 1..NEQ */

static CLC_data clcData = NULL;

static CLC_model clcModel = NULL;

static SD_output simOutput = NULL;

int is_sampled;

#ifdef USE_JACOBIAN
/* Test jacobian */
static int Jac(realtype t, N_Vector y, N_Vector fy, SlsMat JacMat, void *user_data, N_Vector tmp1, N_Vector tmp2, N_Vector tmp3) {

  static int init = 0, n = 0;
  int size = clcData->states, nnz, i, m, j;
  realtype *yval;
  int *colptrs = *JacMat->colptrs;
  int *rowvals = *JacMat->rowvals;

  yval = N_VGetArrayPointer_Serial(y);
  if (!init) {
    SparseSetMatToZero(JacMat);

    for (i=0; i<size; i++) { 
      colptrs[i] = n;
      //printf("Indexes for col %d start at %d.\n", i, n);
      for (j = n, m = 0; j < n + clcData->nSD[i] ; j++, m++) {
        rowvals[j] = clcData->SD[i][m];
        //printf("Non null value at row %d in col %d. Saving it in %d \n", rowvals[j],i, j);
      }
      n += clcData->nSD[i];
    }
    colptrs[i] = n;
    init = 1;
  }
  
  

  for (int i=0; i < n ; i++) 
    JacMat->data[i] = 0;
  clcModel->jac (NV_DATA_S(y), clcData->d, clcData->alg, t, JacMat->data);
  //SparsePrintMat (JacMat, stdout);
  //abort();
  return 0;

}
#endif

/* Test jacobian */
static int check_flag(void *flagvalue, const char *funcname, int opt, CLC_simulator simulator)
{
  int *errflag;

  /* Check if SUNDIALS function returned NULL pointer - no memory allocated */
  if (opt == 0 && flagvalue == NULL) {
     SD_print (simulator->simulationLog, "\nSUNDIALS_ERROR: %s() failed - returned NULL pointer\n\n", funcname);
    return(1); 
  }

  /* Check if flag < 0 */
  else if (opt == 1) {
    errflag = (int *) flagvalue;
    if (*errflag < 0) {
      SD_print (simulator->simulationLog, "\nSUNDIALS_ERROR: %s() failed with flag = %d\n\n", funcname, *errflag);
      return(1); 
    }
  }

  /* Check if function returned NULL pointer - no memory allocated */
  else if (opt == 2 && flagvalue == NULL) {
    SD_print (simulator->simulationLog, "\nMEMORY_ERROR: %s() failed - returned NULL pointer\n\n", funcname);
    SD_print (simulator->simulationLog, "\nMEMORY_ERROR: %s() failed - returned NULL pointer\n\n", funcname);
    return(1); 
  }

  return(0);
}

int CVODE_model (realtype t, N_Vector y, N_Vector ydot, void *user_data) {
  clcData->funEvaluations++;
  clcModel->f (NV_DATA_S(y), clcData->d, clcData->alg, t, NV_DATA_S(ydot));
  return 0;
}


int CVODE_events (realtype t, N_Vector y, realtype *gout, void *user_data) {
  double out;
  int i;
  for (i = 0; i < clcData->events; i++) {
    clcModel->events->zeroCrossing (i, NV_DATA_S(y), clcData->d, clcData->alg, t, &out);
    gout[i] = out + clcData->event[i].zcSign * HIST;
  }
  return 0;
}

void
CVODE_integrate (SIM_simulator simulate)
{
  CLC_simulator simulator = (CLC_simulator) simulate->state->sim;
  clcData = simulator->data;
  clcModel = simulator->model;
  simOutput = simulator->output;
  N_Vector y, abstol;
  void *cvode_mem;
  int i;
  unsigned long totalOutputSteps = 0;
  const double _ft = clcData->ft;
  double dQRel = clcData->dQRel[0];
  double dQMin = clcData->dQMin[0];
  double *_x = clcData->x;
  double step_size = 1;
  is_sampled = simOutput->commInterval != CI_Step;
  if (is_sampled) {
      step_size = simOutput->sampled->period[0];
  }
  const int num_steps = (is_sampled ? _ft / step_size + 1 : MAX_OUTPUT_POINTS);
  double **solution = malloc (sizeof(double*) * simOutput->outputs);
  double *solution_time = malloc (sizeof(double) * num_steps);
  double **outvar = malloc (sizeof(double) * simOutput->outputs);
  int *jroot = malloc (sizeof(int) * clcData->events), flag;
  int size = clcData->states, nnz;
  int event_detected = 0;
  double rel_tol = dQRel, abs_tol = dQMin;
  realtype reltol = rel_tol, t = clcData->it, tout;
  y = abstol = NULL;
  int percentage = 0;
  long int nst=0, nfe=0, nni=0, netf=0, val;
  CLC_compute_outputs (simOutput, solution, num_steps);

  cvode_mem = CVodeCreate(clcData->solver == SD_CVODE_BDF ? CV_BDF : CV_ADAMS, CV_NEWTON);

  y = N_VNew_Serial(size);
  if (check_flag((void *)y, "N_VNew_Serial", 0, simulator)) return;

  abstol = N_VNew_Serial(size);
  if (check_flag((void *)abstol, "N_VNew_Serial", 0, simulator)) return;

  for (i = 0; i < clcData->states; i++) {
    Ith(y,i) = _x[i];
    Ith(abstol,i) = abs_tol;
  }
  
  flag = CVodeInit(cvode_mem, CVODE_model, t, y);
  if (check_flag((void *)cvode_mem, "CVodeCreate", 0, simulator)) return;

  flag = CVodeSVtolerances(cvode_mem, reltol, abstol);
  if (check_flag(&flag, "CVodeInit", 1, simulator)) return;

  flag = CVodeRootInit(cvode_mem, clcData->events, CVODE_events);
  if (check_flag(&flag, "CVodeRootInit", 1, simulator)) return;

/***************************************************************/
  if (clcData->solver == SD_CVODE_BDF_SP) {
    printf("runing sparse CVODE\n");
    nnz = 0;
    for (i=0; i < size; i++)
      nnz += clcData->nSD[i];
    flag = CVSuperLUMT(cvode_mem, 1, size, nnz);
    if (check_flag(&flag, "CVSuperLUMT", 1, simulator)) return;
    /* Set the Jacobian routine to Jac (user-supplied) */
    flag = CVSlsSetSparseJacFn(cvode_mem, Jac);
    if (check_flag(&flag, "CVSlsSetSparseJacFn", 1, simulator)) return;
  } else { 
    printf("runing dense CVODE\n");
    flag = CVDense(cvode_mem, size);
    if (check_flag(&flag, "CVDense", 1, simulator)) return;
  }

/***************************************************************/
  getTime (simulator->stats->sTime);
  if (is_sampled) {
    CLC_save_step (simOutput, solution, solution_time, t, totalOutputSteps, NV_DATA_S(y), clcData->d, clcData->alg);
    totalOutputSteps++;
  }
#ifdef SYNC_RT
  setInitRealTime();
#endif

  while (t < _ft) {
    if (!is_sampled) 
	    tout = _ft;
    else {
	    if (!event_detected)
	      tout = t + step_size;
    }
    if (tout > _ft)
	    tout = _ft;
    if (!is_sampled) 
      flag = CVode(cvode_mem, tout, y, &t, CV_ONE_STEP);
    else
      flag = CVode(cvode_mem, tout, y, &t, CV_NORMAL);
    if (flag == CV_SUCCESS) {
	    CLC_save_step (simOutput, solution, solution_time, t, clcData->totalOutputSteps,NV_DATA_S(y), clcData->d, clcData->alg);
	    clcData->totalOutputSteps++;
	    CLC_save_step (simOutput, solution, solution_time, t, totalOutputSteps,NV_DATA_S(y), clcData->d, clcData->alg);
	    totalOutputSteps++;
      if (is_sampled)
		    tout = t + step_size;
      // Without this line the cummulative of simulation steps returns bogus values
      flag = CVodeGetNumSteps(cvode_mem, &val);
      check_flag(&flag, "CVodeGetNumSteps", 1, simulator);
      nst += val;
      event_detected = 0;
      if (tout > _ft)
        break;
      /*printf("Stepts = %ld\n", val);*/
    } else if (flag == CV_ROOT_RETURN) {
      flag = CVodeGetRootInfo(cvode_mem, jroot);
      if (check_flag(&flag, "CVodeGetRootInfo", 1, simulator)) return;
  	  CLC_handle_event (clcData, clcModel, NV_DATA_S(y), jroot, t, NULL);
      /* Update stats */
      flag = CVodeGetNumSteps(cvode_mem, &val);
      check_flag(&flag, "CVodeGetNumSteps", 1, simulator);
      nst += val;
      flag = CVodeGetNumRhsEvals(cvode_mem, &val);
      check_flag(&flag, "CVodeGetNumRhsEvals" , 1, simulator);
      nfe += val;
      flag = CVodeGetNumErrTestFails(cvode_mem, &val);
      check_flag(&flag, "CVodeGetNumErrTestFails", 1, simulator);
      netf += val;
      flag = CVodeGetNumNonlinSolvIters(cvode_mem, &val);
      check_flag(&flag, "CVodeGetNumNonlinSolvIters", 1, simulator);
      nni += val;
      CVodeReInit(cvode_mem, t, y);
	    if (is_sampled) { // If the root was found close to a sample point take this as the actual step and continue with next sample
	      if (fabs (tout - t) < 1e-12) {
    		  CLC_save_step (simOutput, solution, solution_time, tout, totalOutputSteps, NV_DATA_S(y), clcData->d, clcData->alg);
		      totalOutputSteps++;
    		  tout = t + step_size;
		    }
        event_detected = 1;
      } else { // When a root is found and the per-step output is selected, take roots as outputs
    		  CLC_save_step (simOutput, solution, solution_time, t, totalOutputSteps, NV_DATA_S(y), clcData->d, clcData->alg);
		      totalOutputSteps++;
      }
    } else { 
      SD_print (simulator->simulationLog, "CVODE failed at t=%g\n",t);
      break;
    }
    if ((int) (t * 100 / _ft) > percentage) {
	    percentage = 100 * t / _ft;
	    fprintf (stderr, "*%g", t);
	    fflush (stderr);
	  }
#ifdef SYNC_RT
  waitUntil(t);
#endif
  }
  getTime (simulator->stats->sTime);
  if (is_sampled)
	{
	  if (totalOutputSteps < num_steps)
	    {
	      CLC_save_step (simOutput, solution, solution_time, _ft, totalOutputSteps, NV_DATA_S(y), clcData->d, clcData->alg); 
        totalOutputSteps++;
	    }
	}
  subTime (simulator->stats->sTime, simulator->stats->iTime);
  if (simulator->settings->debug == 0 || simulator->settings->debug > 1)
    {
      check_flag(&flag, "CVodeGetNumSteps", 1, simulator);
      nst += val;
      flag = CVodeGetNumRhsEvals(cvode_mem, &val);
      check_flag(&flag, "CVodeGetNumRhsEvals" , 1, simulator);
      nfe += val;
      flag = CVodeGetNumErrTestFails(cvode_mem, &val);
      check_flag(&flag, "CVodeGetNumErrTestFails", 1, simulator);
      netf += val;
      flag = CVodeGetNumNonlinSolvIters(cvode_mem, &val);
      check_flag(&flag, "CVodeGetNumNonlinSolvIters", 1, simulator);
      nni += val;

      SD_print (simulator->simulationLog, "Simulation time (CVODE %s):", clcData->solver == SD_CVODE_BDF ? "BDF" : "ADAMS");
      SD_print (simulator->simulationLog, "----------------");
      SD_print (simulator->simulationLog, "Miliseconds: %g", getTimeValue (simulator->stats->sTime));
      SD_print (simulator->simulationLog, "Scalar function evaluations: %d", clcData->scalarEvaluations);
      SD_print (simulator->simulationLog, "Individual Zero Crossings : %d", clcData->zeroCrossings);
      SD_print (simulator->simulationLog, "Function evaluations (reported by CVODE): %ld",nfe);
      SD_print (simulator->simulationLog, "Output steps: %d", totalOutputSteps);
      SD_print (simulator->simulationLog, "Simulation steps: %ld", nst);
      SD_print (simulator->simulationLog, "Simulation steps (rejected) : %ld", netf);
      SD_print (simulator->simulationLog, "Newton iterations performed: (reported by CVODE): %ld", nni);
      SD_print (simulator->simulationLog, "Events detected : %d", clcData->totalEvents);
    }
  CLC_write_output (simOutput, solution, solution_time, totalOutputSteps);
  /* Free y and abstol vectors */
  N_VDestroy_Serial(y);
  N_VDestroy_Serial(abstol);

  /* Free integrator memory */
  CVodeFree(&cvode_mem);

  free (outvar);
  free (solution_time);
  free (jroot);
  for (i = 0; i < simOutput->outputs; i++)
      free (solution[i]);
  free (solution);
}
