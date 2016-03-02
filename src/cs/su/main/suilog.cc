/* Copyright (c) Colorado School of Mines, 2011.*/
/* All rights reserved.                       */

/* SUILOG: $Revision: 1.17 $ ; $Date: 2011/11/16 23:21:55 $	*/

#include "csException.h"
#include "csSUTraceManager.h"
#include "csSUArguments.h"
#include "csSUGetPars.h"
#include "su_complex_declarations.h"
#include "cseis_sulib.h"
#include <string>

extern "C" {
  #include <pthread.h>
}
#include "su.h"
#include "segy.h"

/*********************** self documentation **********************/
std::string sdoc_suilog =
"								"
" SUILOG -- time axis inverse log-stretch of seismic traces	"
"								"
" suilog nt= ntmin=  <stdin >stdout 				"
"								"
" Required parameters:						"
" 	nt= 	nt output from sulog prog			"
" 	ntmin= 	ntmin output from sulog prog			"
" 	dt= 	dt output from sulog prog			"
" Optional parameters:						"
" 	none							"
"								"
" NOTE:  Parameters needed by suilog to reconstruct the 	"
"	original data may be input via a parameter file.	"
" 								"
" EXAMPLE PROCESSING SEQUENCE:					"
" 		sulog outpar=logpar <data1 >data2		"
" 		suilog par=logpar <data2 >data3			"
" 								"
" 	where logpar is the parameter file			"
" 								"
;

/*  Sun Feb 24 13:30:07 2013
  Automatically modified for usage in SeaSeis  */
namespace suilog {


/* 
 * Credits:
 *	CWP: Shuki Ronen, Chris Liner
 *
 * Caveats:
 * 	amplitudes are not well preserved
 *
 * Trace header fields accessed: ns, dt
 * Trace header fields modified: ns, dt
 */
/**************** end self doc ********************************/


/* prototypes of functions used internally */
int nextpower(int p, int n); /* function for padding to power of 2 */
void stretch(float *q, float *p, float *w, int *it, int lq, int nw);
void lintrp(float *q, float *w, int *it, int lp, int lq);

segy tr;

void* main_suilog( void* args )
{
	float *buf;	/* temp repository of unstretched data		*/
	float dt;	/* sampling interval on original data		*/
	float dtau;	/* sampling interval on stretched data		*/
	float *tau;	/* fractional sample number on input data	*/
	float *w;	/* Interpolation weights 			*/
	int it;		/* output time sample counter			*/
	int *itau;	/* Interpolation locations			*/
	int nt;		/* samples in reconstructed outdata		*/
	size_t ntsize;	/*  ... in bytes				*/
	int ntau;	/* samples in input log-stretch data		*/
	int ntmin;	/* minumum time of interest (from par)		*/
	int nw;		/* Number of interpolation weights (2=linear)	*/


	/* Initialize */
	cseis_su::csSUArguments* suArgs = (cseis_su::csSUArguments*)args;
	cseis_su::csSUTraceManager* cs2su = suArgs->cs2su;
	cseis_su::csSUTraceManager* su2cs = suArgs->su2cs;
	int argc = suArgs->argc;
	char **argv = suArgs->argv;
	cseis_su::csSUGetPars parObj;

	void* retPtr = NULL;  /*  Dummy pointer for return statement  */
	su2cs->setSUDoc( sdoc_suilog );
	if( su2cs->isDocRequestOnly() ) return retPtr;
	parObj.initargs(argc, argv);

	try {  /* Try-catch block encompassing the main function body */



	/* Get information from the first header */
	if (!cs2su->getTrace(&tr)) throw cseis_geolib::csException("can't get first trace");
	ntau = tr.ns;
	dtau = ((double) tr.dt)/1000000.0;

	/* Must get nt and ntmin for reconstruction of t-data */
	CSMUSTGETPARINT("nt", &nt); CHECK_NT("nt",nt);
	CSMUSTGETPARINT("ntmin", &ntmin); 
	CSMUSTGETPARFLOAT("dt", &dt); 
        parObj.checkpars();
	ntsize = nt * FSIZE;

	/* 2 weights for linear interpolation */
	nw = 2;
	
	/* Allocate space for stretching operation */
	tau = ealloc1float(nt);
	itau = ealloc1int(nt);
	w = ealloc1float(nw * nt);
	buf = ealloc1float(nt);


/* 	The inverse log-stretch from 'tau' to 't' is given mathematically by 
 *  								
 * 		t = tmin exp (tau + taumin)            
 *  							
 * 	taumin is arbitrary and taken to be taumin=0	
 */

	/* Calculate fractional tau-sample that each t-sample maps to; */
	/* [ it=itmin --> 0 ..&.. it=nt --> ntau ]  */
	for (it = 0; it < ntmin; ++it) {
		tau[it] = -1;	/* neg flag for zero value */
	}
	for (it = ntmin; it < nt; ++it) {
		tau[it] = log((float) it/ntmin) / dtau;
	}

	/* Calculate the interpolation coefficients */
	lintrp (tau, w, itau, ntau, nt);

	/* Main loop over traces */
	do {
		/* Perform the stretch; put new data into buf */
		stretch (buf, tr.data, w, itau, nt, nw);

		/* Overwrite the segy data */
		memcpy(tr.data, buf, ntsize);

		tr.ns = nt;
		tr.dt = dt;

		su2cs->putTrace(&tr);

	} while ( cs2su->getTrace(&tr) );


	su2cs->setEOF();
	pthread_exit(NULL);
	return retPtr;
}
catch( cseis_geolib::csException& exc ) {
  su2cs->setError("%s",exc.getMessage());
  pthread_exit(NULL);
  return retPtr;
}
}

void stretch(float *q, float *p, float *w, int *it, int lq, int nw)
/*
 *  General coordinate stretch with predetermined coefficients
 *
 *         NW-1
 * Q(T) =  SUM W(T,J)*P(IT(T)), FOR T=0,LQ-1
 *         J=0
 */
{
	int j, i;

	for (i = 0; i < lq; ++i) {
		q[i] = 0.0;
		for (j = 0; j < nw; ++j) {
			q[i] += w[i*nw+j] * p[it[i]+j];
		}
	}
	return;
}

void lintrp (float *q, float *w, int *it, int lp, int lq)
{
	int i;
	float delta;

	for (i = 0; i < lq; ++i) {
		if (q[i] >= 0.0 && q[i] < lp - 1) {
			it[i] = q[i]; 
			delta = q[i] - it[i];
			w[i*2] = 1.0 - delta;
			w[i*2+1] = delta;
		} else {
			it[i] = 0;
			w[i*2] = 0.0;
			w[i*2+1] = 0.0;
		}
	}
	return;
}

} // END namespace
