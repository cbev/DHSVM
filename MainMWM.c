/*
 * SUMMARY:      MainMWM.c - Mass Wasting Module
 * USAGE:        MWM
 *
 * AUTHOR:       Colleen O. Doten/Laura Bowling
 * ORG:          University of Washington, Department of Civil Engineering
 * E-MAIL:       dhsvm@hydro.washington.edu
 * ORIG-DATE:    Sep-02
 * Last Change: Thu Jun 19 09:27:02 2003 by Ed Maurer <edm@u.washington.edu>
 * DESCRIPTION:  Main routine to drive MWM - the Mass Wasting Module for DHSVM 
 * DESCRIP-END.   
 cd
 * FUNCTIONS:    main()
 * COMMENTS:
 */

/******************************************************************************/
/*			     INCLUDES                                  */
/******************************************************************************/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "settings.h"
#include "Calendar.h"
#include "getinit.h"
#include "DHSVMerror.h"
#include "data.h"
#include "fileio.h"
#include "functions.h"
#include "constants.h"
#include "DHSVMChannel.h"
#include "slopeaspect.h"

#define BUFSIZE      255
#define empty(s) !(s)

void enqueue(node **head, node **tail, int y, int x);
void dequeue(node **head, node **tail, int *y, int *x);

/******************************************************************************/
/*			       MAIN                                    */
/******************************************************************************/
void MainMWM(SEDPIX **SedMap, FINEPIX *** FineMap, VEGTABLE *VType,
	     SEDTABLE *SedType, CHANNEL *ChannelData, char *DumpPath, 
	     SOILPIX **SoilMap, TIMESTRUCT *Time, MAPSIZE *Map,
	     TOPOPIX **TopoMap, SOILTABLE *SType, VEGPIX **VegMap,
	     int MaxStreamID, SNOWPIX **SnowMap) 
{
  int x,y,xx,yy,i,j,ii,jj,k,iter;  /* Counters. */
  int coursei, coursej;
  int nextx, nexty;
  int prevx, prevy;
  int numfailures;
  char buffer[32];
  char sumoutfile[100], outfile[100];  /* Character arrays to hold file name. */ 
  int **failure;
  float factor_safety;
  float LocalSlope;
  FILE *fs, *fo;                  /* File pointers. */
  int numpixels;
  int cells, count, checksink;
  int massitertemp;               /* if massiter is 0, sets the counter to 1 here */
  float TotalVolume;
  node *head, *tail;
  float SlopeAspect, SedimentToChannel;
  float *SegmentSediment;         /* The cumulative sediment content over all stochastic
				     iterations for each channel segment. */
  float *InitialSegmentSediment; /* Placeholder of segment sediment load at 
				    beginning of time step. */
  float **SedThickness;          /* Cumulative sediment depth over all stochastic
				    iterations for each pixel.  */
  float **InitialSediment;       /* Place holder of pixel sediment load at beginning of
				    time step. */
  float SedToDownslope;		/* Sediment wasted from a pixel, awaiting redistribution */
  float SedFromUpslope;		/* Wasted sediment being redistributed */
  float *SedDiams;
  float FineMapTableDepth;       /* Fine grid water table depth (m) */
  float TableDepth;              /* Coarse grid water table depth (m) */
  float FineMapSatThickness;    /* Fine grid saturated thickness (m) */
  float **Redistribute, **TopoIndex, **TopoIndexAve;
  head = NULL;
  tail = NULL;

  /*****************************************************************************
   Allocate memory 
  ****************************************************************************/
  if (!(Redistribute = (float **)calloc(Map->NY, sizeof(float *))))
    ReportError("MainMWM", 1);
  for(i=0; i<Map->NY; i++) {
    if (!(Redistribute[i] = (float *)calloc(Map->NX, sizeof(float))))
      ReportError("MainMWM", 1);
  }

  if (!(TopoIndex = (float **)calloc(Map->NY, sizeof(float *))))
    ReportError("MainMWM", 1);
  for(i=0; i<Map->NY; i++) {
      if (!(TopoIndex[i] = (float *)calloc(Map->NX, sizeof(float))))
	ReportError("MainMWM", 1);
  }

  if (!(TopoIndexAve = (float **)calloc(Map->NY, sizeof(float *))))
    ReportError("MainMWM", 1);
  for(i=0; i<Map->NY; i++) {
      if (!(TopoIndexAve[i] = (float *)calloc(Map->NX, sizeof(float))))
	ReportError("MainMWM", 1);
  }
  
  if (!(failure = (int **)calloc(Map->NYfine, sizeof(int *))))
    ReportError("MainMWM", 1);
  for(i=0; i<Map->NYfine; i++) {
      if (!(failure[i] = (int *)calloc(Map->NXfine, sizeof(int))))
	ReportError("MainMWM", 1);
  }

  if (!(SedThickness = (float **)calloc(Map->NYfine, sizeof(float *))))
    ReportError("MainMWM", 1);
  for(i=0; i<Map->NYfine; i++) {
      if (!(SedThickness[i] = (float *)calloc(Map->NXfine, sizeof(float))))
	ReportError("MainMWM", 1);
  }

  if (!(InitialSediment = (float **)calloc(Map->NYfine, sizeof(float *))))
    ReportError("MainMWM", 1);
  for(i=0; i<Map->NYfine; i++) {
      if (!(InitialSediment[i] = (float *)calloc(Map->NXfine, sizeof(float))))
	ReportError("MainMWM", 1);
  }

  if (!(SegmentSediment = (float *)calloc(MaxStreamID, sizeof(float ))))
    ReportError("MainMWM", 1);

  if (!(SedDiams = (float *)calloc(NSEDSIZES, sizeof(float))))
    ReportError("MainMWM", 1);

  if (!(InitialSegmentSediment = (float *)calloc(NSEDSIZES, sizeof(float ))))
    ReportError("MainMWM", 1);

  /*****************************************************************************
  Perform Calculations 
  *****************************************************************************/
  /* Redistribute soil moisture from coarse grid to fine grid. The is done similarly
     to Burton, A. and J.C. Bathurst, 1998, Physically based modelling of shallot landslide 
     sediment yield as a catchment scale, Environmental Geology, 35 (2-3), 89-99.*/
 
  /* This could be moved elsewhere. */
  for (i = 0; i < Map->NY; i++) {
    for (j = 0; j < Map->NX; j++) {
      
      /* Check to make sure region is in the basin. */
      if (INBASIN(TopoMap[i][j].Mask)) {
	
        /* Step over each fine resolution cell within the model grid cell. */
	for(ii=0; ii< Map->DY/Map->DMASS; ii++) { /* Fine resolution counters. */
	  for(jj=0; jj< Map->DX/Map->DMASS; jj++) {
	    y = (int) i*Map->DY/Map->DMASS + ii;
	    x = (int) j*Map->DX/Map->DMASS + jj;
	    
	    TopoIndex[i][j] += (*FineMap)[y][x].TopoIndex;
	    
	  }
	}
	/* TopoIndexAve is the TopoIndex for the coarse grid calculated as the average of the 
	   TopoIndex of the fine grids in the coarse grid. */
	TopoIndexAve[i][j] = TopoIndex[i][j]/Map->NumFineIn;
      }
    }
  }
  
  FineMapSatThickness = 0.;
  
  for (i = 0; i < Map->NY; i++) {
    for (j  = 0; j < Map->NX; j++) {
      if (INBASIN(TopoMap[i][j].Mask)) {
	
	TableDepth = SoilMap[i][j].TableDepth;
	
	/* Do not want to distribute ponded water  */
	if (TableDepth < 0)
	  TableDepth = 0;
		
	for(ii=0; ii< Map->DY/Map->DMASS; ii++) {
	  for(jj=0; jj< Map->DX/Map->DMASS; jj++) {
	    y = (int) i*Map->DY/Map->DMASS + ii;
	    x = (int) j*Map->DX/Map->DMASS + jj;
	    
	    FineMapTableDepth = TableDepth + 
	      ((TopoIndexAve[i][j]-(*FineMap)[y][x].TopoIndex)/ 
	       SType[SoilMap[i][j].Soil - 1].KsLatExp);
	    
	  
	    if (FineMapTableDepth < 0)
	      (*FineMap)[y][x].SatThickness = (*FineMap)[y][x].sediment; 
	      
	    else if (FineMapTableDepth > (*FineMap)[y][x].sediment)
	      (*FineMap)[y][x].SatThickness = 0; 
	      
	    else 
	      (*FineMap)[y][x].SatThickness = (*FineMap)[y][x].sediment -
		FineMapTableDepth;
	  

	    FineMapSatThickness += (*FineMap)[y][x].SatThickness;
	    
	    if ((ii== Map->DY/Map->DMASS - 1) & (jj== Map->DX/Map->DMASS - 1)){
	    
	      /* Calculating the difference between the volume of water distributed
		 (only saturated) and available volume of water (m3)*/
	      Redistribute[i][j] = (Map->DY * Map->DX *(SoilMap[i][j].Depth - TableDepth)) - 
		(FineMapSatThickness*Map->DMASS*Map->DMASS); 
	      FineMapSatThickness = 0;
	    }
	  }
	}
      }
    }
  }

  /* Redistribute volume difference. Start with cells with too much water. */ 
  for (i = 0; i < Map->NY; i++) {
    for (j  = 0; j < Map->NX; j++) {
      if (INBASIN(TopoMap[i][j].Mask)) {
	
	if (Redistribute[i][j]< -25.){

	  for (k = 0; k < Map->NumFineIn; k++) { 
	    y = TopoMap[i][j].OrderedTopoIndex[k].y;
	    x = TopoMap[i][j].OrderedTopoIndex[k].x;
	    yy = TopoMap[i][j].OrderedTopoIndex[(Map->NumFineIn)-k-1].y;
	    xx = TopoMap[i][j].OrderedTopoIndex[(Map->NumFineIn)-k-1].x;
	    
	    /* Convert sat thickness to a volume */
	    (*FineMap)[y][x].SatThickness *= (Map->DMASS)*(Map->DMASS);
	    /* Add to volume based on amount to be redistributed */
	    (*FineMap)[y][x].SatThickness += Redistribute[i][j] * ((*FineMap)[yy][xx].TopoIndex/TopoIndex[i][j]); 
	    /* Convert back to thickness (m)*/
	    (*FineMap)[y][x].SatThickness /= (Map->DMASS)*(Map->DMASS);

	  }
	}
      }
    }
  }

  /* Redistribute volume difference for cells with too little water.*/ 
  for (i = 0; i < Map->NY; i++) {
    for (j  = 0; j < Map->NX; j++) {
      
      if (INBASIN(TopoMap[i][j].Mask)) {
	
	for(ii=0; ii< Map->DY/Map->DMASS; ii++) {
	  for(jj=0; jj< Map->DX/Map->DMASS; jj++) {
	    y = (int) i*Map->DY/Map->DMASS + ii;
	    x = (int) j*Map->DX/Map->DMASS + jj;
	    
	    if (Redistribute[i][j] > 25.){

	      /* Convert sat thickness to a volume */
	      (*FineMap)[y][x].SatThickness *= (Map->DMASS)*(Map->DMASS);
	      /* Add to volume based on amount to be redistributed */
	      (*FineMap)[y][x].SatThickness += Redistribute[i][j] * ((*FineMap)[y][x].TopoIndex/TopoIndex[i][j]);
	      /* Convert back to thickness (m)*/
	      (*FineMap)[y][x].SatThickness /= (Map->DMASS)*(Map->DMASS);

	    }
	    
	    if ((Redistribute[i][j] > 25.)||(Redistribute[i][j]< -25.)){
	      
	      if ((*FineMap)[y][x].SatThickness > (*FineMap)[y][x].sediment)
		(*FineMap)[y][x].SatThickness = (*FineMap)[y][x].sediment; 
	      
	      else if ((*FineMap)[y][x].SatThickness < 0)
		(*FineMap)[y][x].SatThickness = 0.;
	    }
	  }
	}
      }
    }
  }	      
	 
 for(i=0; i<Map->NY; i++) { 
    free(Redistribute[i]);
    free(TopoIndex[i]);
    free(TopoIndexAve[i]);
 }
 free(Redistribute);
 free(TopoIndex);
 free(TopoIndexAve);

/* for (i = 0; i < Map->NY; i++) { */
/*     for (j  = 0; j < Map->NX; j++) { */

/*       if (INBASIN(TopoMap[i][j].Mask)) { */
/* 	for(ii=0; ii< Map->DY/Map->DMASS; ii++) { */
/* 	  for(jj=0; jj< Map->DX/Map->DMASS; jj++) { */
/* 	    y = (int) i*Map->DY/Map->DMASS + ii; */
/* 	    x = (int) j*Map->DX/Map->DMASS + jj; */

/* 	    (*FineMap)[y][x].SatThickness = SoilMap[i][j].Depth - SoilMap[i][j].TableDepth; */
/* 	  } */
/* 	} */
/*       } */
/*     } */
/*   } */

  /* Initialize arrays. */
  for (i = 0; i < Map->NYfine; i++) {
    for (j  = 0; j < Map->NXfine; j++) {
    
      InitialSediment[i][j] = (*FineMap)[i][j].sediment;
      (*FineMap)[i][j].Probability = 0.;
      (*FineMap)[i][j].MassWasting = 0.;
      (*FineMap)[i][j].MassDeposition = 0.;
      (*FineMap)[i][j].SedimentToChannel = 0.;
    }
  }

  update_sediment_array(ChannelData->streams, InitialSegmentSediment);
	    
  /************************************************************************/
  /* Begin iteration for multiple ensembles. */
  /************************************************************************/

  /* sloppy fix for when MASSITER=0 -- this only needs to be checked once */
  if(MASSITER==0) massitertemp=1;
  else massitertemp=MASSITER;
  
  for(iter=0; iter < massitertemp; iter++) {
    
    fprintf(stderr,"iter=%d\n",iter);
    
    /************************************************************************/
    /* Begin factor of safety code. */
    /************************************************************************/
    for(i=0; i<Map->NY; i++) {
      for(j=0; j<Map->NX; j++) {
	if (INBASIN(TopoMap[i][j].Mask)) {		
	  
	  for(ii=0; ii<Map->DY/Map->DMASS; ii++) {
	    for(jj=0; jj<Map->DX/Map->DMASS; jj++) {
	      y = i*Map->DY/Map->DMASS + ii; 
	      x = j*Map->DX/Map->DMASS + jj;
	      coursei = i;
	      coursej = j;
	      
	      checksink = 0;
	      numpixels = 0;
	      SedToDownslope = 0.0;
	      SedFromUpslope = 0.0;
	      SedimentToChannel = 0.0;
	      /* First check for original failure. */
	      if((*FineMap)[y][x].SatThickness/SoilMap[i][j].Depth > MTHRESH && failure[y][x] == 0
	        && (*FineMap)[y][x].sediment > 0.0) {

		LocalSlope = ElevationSlope(Map, FineMap, y, x, &nexty, &nextx, y, x, &SlopeAspect);
	
		/* Slopes >45 are not likely to have soil. Also, factor of safety 
		   increases (indicating slope is more stable) with slope for slopes 
		   greater than 45 degrees. */
		if(LocalSlope >= 10. && LocalSlope <= 45.) { 
		  factor_safety = CalcSafetyFactor(LocalSlope, SoilMap[i][j].Soil, 
						   (*FineMap)[y][x].sediment, 
						   VegMap[i][j].Veg, SedType, VType, 
						   (*FineMap)[y][x].SatThickness, SType, 
						   SnowMap[i][j].Swq, SnowMap[i][j].Depth);
		  
		  /* check if fine pixel fails */
		  if (factor_safety < 1 && factor_safety > 0) {
		    numpixels = 1;
		    failure[y][x] = 1;	      
		    
		    /* Update sediment depth. All sediment leaves failed fine pixel */
		    SedToDownslope = (*FineMap)[y][x].sediment;
		    (*FineMap)[y][x].sediment = 0.0;
//fprintf(stdout,"y %d x %d primary failure; SedToDownslope %f\n",y,x,SedToDownslope*(Map->DMASS*Map->DMASS));

		    // Pass sediment down to next pixel
		    SedFromUpslope = SedToDownslope;

		    /* Follow failures down slope; skipped if no original failure. */
		    while(failure[y][x] == 1 && checksink == 0 
		      && !channel_grid_has_channel(ChannelData->stream_map, coursej, coursei)
		      && INBASIN(TopoMap[coursei][coursej].Mask)) {
		
		      /* Update counters. */
		      prevy = y;
		      prevx = x;
		      y = nexty;
		      x = nextx;
		      coursei = floor(y*Map->DMASS/Map->DY);
		      coursej = floor(x*Map->DMASS/Map->DX);
		
		      if (!INBASIN(TopoMap[coursei][coursej].Mask)) {

		        fprintf(stderr,"WARNING: attempt to propagate failure to grid cell outside basin: y %d x %d\n",y,x);
		        fprintf(stderr,"Depositing wasted sediment in grid cell y %d x %d\n",prevy,prevx);
			(*FineMap)[prevy][prevx].sediment += SedFromUpslope;
			SedFromUpslope = SedToDownslope = 0.0;

			// Since we're returning SedFromUpslope to the upslope pixel,
			// the upslope pixel can't be considered as part of the failure
			failure[prevy][prevx] = 0;

		      }
		      else {

		        // Add sediment from upslope to current sediment
		        (*FineMap)[y][x].sediment += SedFromUpslope;
//fprintf(stdout,"y %d x %d depositing %f total sediment %f\n",y,x,SedFromUpslope*(Map->DMASS*Map->DMASS),(*FineMap)[y][x].sediment*(Map->DMASS*Map->DMASS));
		
		        LocalSlope = ElevationSlope(Map, FineMap, y, x, &nexty, 
					            &nextx, prevy, prevx, &SlopeAspect);
		        /*  Check that not a sink */
		        if(LocalSlope >= 0) {
	
		          factor_safety = CalcSafetyFactor(LocalSlope, SoilMap[coursei][coursej].Soil, 
						           (*FineMap)[y][x].sediment, 
						           VegMap[coursei][coursej].Veg, SedType, VType,
						           (*FineMap)[y][x].SatThickness, SType,
						           SnowMap[coursei][coursej].Swq, 
						           SnowMap[coursei][coursej].Depth);
			
		          /* check if fine pixel fails */
		          if (factor_safety < 1 && factor_safety > 0) {
		            numpixels += 1;
		            failure[y][x] = 1;
		    
		            /* Update sediment depth. All sediment leaves failed fine pixel */
		            SedToDownslope = (*FineMap)[y][x].sediment;
		            (*FineMap)[y][x].sediment = 0.0;
//fprintf(stdout,"y %d x %d secondary failure; SedFromUpslope %f SedToDownslope %f\n",y,x,SedFromUpslope*(Map->DMASS*Map->DMASS),SedToDownslope*(Map->DMASS*Map->DMASS));

			    // Pass sediment down to next pixel
		            SedFromUpslope = SedToDownslope;

		          }
		          else {
			    /* Update sediment depth. */
			    // Remove the sediment we added for the slope calculation
			    // and instead prepare to distribute this sediment along runout zone
                            (*FineMap)[y][x].sediment -= SedFromUpslope;
//fprintf(stdout,"y %d x %d no secondary failure; will redistribute %f in runout zone\n",y,x,SedFromUpslope*(Map->DMASS*Map->DMASS));

			  }
		        } /* end  if(LocalSlope >= 0) { */
		        else {
			  /* Update sediment depth. */
			  // Remove the sediment we added for the slope calculation
			  // and instead prepare to distribute this sediment along runout zone
                       //   (*FineMap)[y][x].sediment -= SedFromUpslope;
//fprintf(stdout,"y %d x %d no secondary failure; will redistribute %f in runout zone\n",y,x,SedFromUpslope*(Map->DMASS*Map->DMASS));
                        /* COD - if it reaches here, a sink exists. A sink can 
			   not fail or run out, so move to the next pixel. */
			  checksink++;

			}

		      }

	            }  /* End of while loop. */

		    if (checksink > 0) continue;
		
	            /* Failure has stopped, now calculate runout distance and 
		       redistribute sediment. */
		
		    // y and x are now the coords of the first pixel of the runout
		    // (downslope neighbor of final pixel of the failure);
		    // this is the pixel that caused the last loop to exit,
		    // whether due to being a sink, not failing,
		    // being in a coarse pixel containing a stream,
		    // or being outside the basin

		    // If current cell is outside the basin,
		    // stop processing this runout and go to next failure candidate
		    if (!INBASIN(TopoMap[coursei][coursej].Mask)) {
		      continue;
		    }

		    // TotalVolume = depth (not volume) being redistributed
		    TotalVolume = SedFromUpslope;
//fprintf(stdout,"y %d x %d current total runout volume %f\n",y,x,TotalVolume*(Map->DMASS*Map->DMASS));

		    cells = 1;
		
		    /* queue begins with initial unfailed pixel. */
		    enqueue(&head, &tail, y, x); 
		
		    while(LocalSlope > 4. && !channel_grid_has_channel(ChannelData->stream_map, coursej, coursei)
		      && INBASIN(TopoMap[coursei][coursej].Mask)) {
		      /* Redistribution stops if last pixel was a channel
		         or was outside the basin. */
		      /* Update counters. */
		      prevy = y;
		      prevx = x;
		      y = nexty;
		      x = nextx;
		      coursei = floor(y*Map->DMASS/Map->DY);
		      coursej = floor(x*Map->DMASS/Map->DX);
		  
		      if (INBASIN(TopoMap[coursei][coursej].Mask)) {
		
			LocalSlope = ElevationSlope(Map, FineMap, y, x, &nexty,
					            &nextx, prevy, prevx,
					            &SlopeAspect);
		        enqueue(&head, &tail, y, x);
		        cells++;
		      }
		      else {
			fprintf(stderr,"WARNING: attempt to propagate runout to grid cell outside the basin: y %d x %d\n",y,x);
			fprintf(stderr,"Final grid cell of runout will be: y %d x %d\n",prevy,prevx);
		      }
		    }
		    prevy = y;
		    prevx = x;
		
		    for(count=0; count < cells; count++) {
		      dequeue(&head, &tail, &y, &x);
		      coursei = floor(y*Map->DMASS/Map->DY);
		      coursej = floor(x*Map->DMASS/Map->DX);

//if (!INBASIN(TopoMap[coursei][coursej].Mask)) {
//fprintf(stdout,"OUTSIDE BASIN!! We shouldn't be here\n");
//}
		  
		      // If this node has a channel, then this MUST be the end of the queue
		      if(channel_grid_has_channel(ChannelData->stream_map, coursej, coursei)) {
		        /* TotalVolume at this point is a depth in m over one fine
		           map grid cell - convert to m3 */
		        SedimentToChannel = TotalVolume*(Map->DMASS*Map->DMASS)/(float) cells;
//fprintf(stdout,"count %d y %d x %d current SedimentToChannel %f\n",count,y,x,SedimentToChannel);
		      }
		      else {
		        /* Redistribute sediment equally among all hillslope cells. */
//fprintf(stdout,"count %d y %d x %d runout dep. %f\n",count,y,x,TotalVolume*(Map->DMASS*Map->DMASS)/cells);
		        (*FineMap)[y][x].sediment += TotalVolume/cells;
		      }
		    }
		
		    if(SedimentToChannel > 0.0) {
		      if(SlopeAspect < 0.) {
		        fprintf(stderr, "Invalid aspect (%3.1f) in cell y= %d x= %d\n",
			        SlopeAspect,y,x);
		        exit(0);
		      }
		      else {

		        // Add Current value of SedimentToChannel to running total for this FineMap cell
		        // (allowing for more than one debris flow to end at the same channel)
		        (*FineMap)[y][x].SedimentToChannel += SedimentToChannel;

                        // Now route SedimentToChannel through stream network
// How is this "averaged" over the mass iterations???
		        RouteDebrisFlow(&SedimentToChannel, coursei, coursej, SlopeAspect, ChannelData, Map); 

		      }
		    }
		  } /* End of this failure/runout event */
		
		}		      
	      }
	      
	    }  /* End of jj loop. */
	  }
	}
	
	
      }       
    }    /* End of course resolution loop. */

    /* Record failures and Reset failure map for new iteration. */
    for(i=0; i<Map->NY; i++) {
      for(j=0; j<Map->NX; j++) {
	if (INBASIN(TopoMap[i][j].Mask)) {		
	  
	  for(ii=0; ii<Map->DY/Map->DMASS; ii++) {
	    for(jj=0; jj<Map->DX/Map->DMASS; jj++) {
	      y = i*Map->DY/Map->DMASS + ii; 
	      x = j*Map->DX/Map->DMASS + jj;
	      
	      (*FineMap)[y][x].Probability += (float) failure[y][x];
	  
	      /* Record cumulative sediment volume. */
	      SedThickness[y][x] += (*FineMap)[y][x].sediment;
	  
	      /* Reset sediment thickness for each iteration; otherwise there is 
	         a decreasing probability of failure for subsequent iterations. */
	      /* If not in stochastic mode, then allow a history of past failures
	         and do not reset sediment depth */
	      if(massitertemp>1) {
	        (*FineMap)[y][x].sediment = InitialSediment[y][x];
	        failure[y][x] = 0;
	      }

	    }
	  }

	}
	else {

	  for(ii=0; ii<Map->DY/Map->DMASS; ii++) {
	    for(jj=0; jj<Map->DX/Map->DMASS; jj++) {
	      y = i*Map->DY/Map->DMASS + ii; 
	      x = j*Map->DX/Map->DMASS + jj;
	      (*FineMap)[y][x].Probability = -999.;
	      (*FineMap)[y][x].DeltaDepth = -999.;
	    }
	  }

	}
      }
    }

    /* Record cumulative stream sediment volumes. */
    initialize_sediment_array(ChannelData->streams, SegmentSediment);

    /* Reset channel sediment volume for each iteration. */
    update_sediment_array(ChannelData->streams, InitialSegmentSediment);
   
    
  }    /* End iteration loop */

  /*************************************************************************/
  /* Create output files...currently hard-coded, should be moved to dump   */
  /* functions for user specification. Creates the following files in the  */
  /* specified output directory:                                           */ 
  /* failure_summary.txt - no. of failed pixels for each date that the mwm */
  /*                       algorithm is run.  Failed pixel = probability   */
  /*                       of failure > 0.5                                */ 
  /* "date"_failure.txt - map of probability of failure for each pixel     */
  /* "date"_Deltasoildepth.txt - map of cumulative change in soil depth    */
  /*                             since beginning of model run.             */
  /*************************************************************************/

  numfailures = 0;
  for(i=0; i<Map->NY; i++) {
    for(j=0; j<Map->NX; j++) {
      if (INBASIN(TopoMap[i][j].Mask)) {		
	  
	for(ii=0; ii<Map->DY/Map->DMASS; ii++) {
	  for(jj=0; jj<Map->DX/Map->DMASS; jj++) {
	    y = i*Map->DY/Map->DMASS + ii; 
	    x = j*Map->DX/Map->DMASS + jj;
	      
	    (*FineMap)[y][x].Probability /= (float)massitertemp;
	    (*FineMap)[y][x].sediment = SedThickness[y][x]/(float)massitertemp;
	    (*FineMap)[y][x].SedimentToChannel /= (float)massitertemp;

	    if ((*FineMap)[y][x].sediment > InitialSediment[y][x]) {
	      (*FineMap)[y][x].MassDeposition = ((*FineMap)[y][x].sediment - InitialSediment[y][x])*(Map->DMASS*Map->DMASS);
	      (*FineMap)[y][x].MassWasting = 0.0;
//fprintf(stdout,"y %d x %d i %d j %d MassDeposition %f\n",y,x,i,j,(*FineMap)[y][x].MassDeposition);
	    }
	    else if ((*FineMap)[y][x].sediment < InitialSediment[y][x]) {
	      (*FineMap)[y][x].MassDeposition = 0.0;
	      (*FineMap)[y][x].MassWasting = (InitialSediment[y][x] - (*FineMap)[y][x].sediment)*(Map->DMASS*Map->DMASS);
//fprintf(stdout,"y %d x %d i %d j %d MassWasting %f\n",y,x,i,j,(*FineMap)[y][x].MassWasting);
	    }
	    if((*FineMap)[y][x].Probability > .5)
	      numfailures +=1;
	  
	    (*FineMap)[y][x].DeltaDepth = (*FineMap)[y][x].sediment - 
	      SoilMap[i][j].Depth;

	  }
	}

      }
      else {

	for(ii=0; ii<Map->DY/Map->DMASS; ii++) {
	  for(jj=0; jj<Map->DX/Map->DMASS; jj++) {
	    y = i*Map->DY/Map->DMASS + ii; 
	    x = j*Map->DX/Map->DMASS + jj;
	    (*FineMap)[y][x].Probability = OUTSIDEBASIN;
	    (*FineMap)[y][x].DeltaDepth = OUTSIDEBASIN;
	    (*FineMap)[y][x].sediment = OUTSIDEBASIN;
	  }
	}

      }

    }
  }

  /*average sediment delivery to each stream segment***************/
  for(i=0; i<MaxStreamID; i++) {
    SegmentSediment[i] /= (float)massitertemp;
    if(SegmentSediment[i] < 0.0) SegmentSediment[i]=0.0;
    /*    fprintf(stderr, "segment %d: segmentsediment = %f\n", i, 
	  SegmentSediment[i]-InitialSegmentSediment[i]); */
  }
  update_sediment_array(ChannelData->streams, SegmentSediment);
  /* Take new sediment inflow and distribute it by representative diameters*/
  /* and convert to mass */
  sed_vol_to_distrib_mass(ChannelData->streams, SegmentSediment);
  /* back to map analysis******************************************/

  sprintf(sumoutfile, "%sfailure_summary.txt", DumpPath);

  if((fs=fopen(sumoutfile,"a")) == NULL)
    {
      fprintf(stderr,"Cannot open factor of safety summary output file.\n");
      exit(0);
    }

  SPrintDate(&(Time->Current), buffer);
  fprintf(fs, "%-20s %7d\n", buffer, numfailures); 
  fprintf(stderr, "%d pixels failed\n", numfailures);
  fclose(fs);

  /* If any pixels failed, output map of failure probabilities & deltasoildepth. 
     Note: no failures does not mean that the probability of failure is zero. */ 
     
  if(numfailures > 0) {
    sprintf(outfile, "%s%s_failure.txt", DumpPath, buffer);

    if((fo=fopen(outfile,"w")) == NULL) {
      fprintf(stderr,"Cannot open factor of safety output file.\n");
      exit(0);
    }
	
    /* Printing header to output file */
    fprintf(fo,"ncols %11d\n",Map->NXfine);
    fprintf(fo,"nrows %11d\n",Map->NYfine);
    fprintf(fo,"xllcorner %.1f\n",Map->Xorig);
    fprintf(fo,"yllcorner %.1f\n",Map->Yorig - Map->NY*Map->DY);
    fprintf(fo,"cellsize %.0f\n",Map->DMASS);
    fprintf(fo,"NODATA_value %.2f\n",-999.);

    for (i = 0; i < Map->NYfine; i++) {
      for (j  = 0; j < Map->NXfine; j++) {
      
	y = (int) floor(i/(Map->DY/Map->DMASS));
	x = (int) floor(j/(Map->DX/Map->DMASS));

	/* Check to make sure region is in the basin. */
	if (INBASIN(TopoMap[y][x].Mask)) 		
	  fprintf(fo, "%.2f ", (*FineMap)[i][j].Probability);
	else
	  fprintf(fo, "-999. ");
  
      }
      fprintf(fo, "\n");
    }
	
    sprintf(sumoutfile, "%s%s_Deltasoildepth.txt", DumpPath, buffer);
    if((fs=fopen(sumoutfile,"w")) == NULL) {
      fprintf(stderr,"Cannot open soil depth output file.\n");
      exit(0);
    }

    /* Printing header to output file */
    fprintf(fs,"ncols %11d\n",Map->NXfine);
    fprintf(fs,"nrows %11d\n",Map->NYfine);
    fprintf(fs,"xllcorner %.1f\n",Map->Xorig);
    fprintf(fs,"yllcorner %.1f\n",Map->Yorig - Map->NY*Map->DY);
    fprintf(fs,"cellsize %.0f\n",Map->DMASS);
    fprintf(fs,"NODATA_value %.2f\n",-999.);

    for (i = 0; i < Map->NYfine; i++) {
      for (j  = 0; j < Map->NXfine; j++) {
      
        y = (int) floor(i/(Map->DY/Map->DMASS));
        x = (int) floor(j/(Map->DX/Map->DMASS));

        /* Check to make sure region is in the basin. */
        if (INBASIN(TopoMap[y][x].Mask)) 		
	  fprintf(fs, "%.2f ", (*FineMap)[i][j].DeltaDepth);
        else
	  fprintf(fs, "-999. ");
      }
      fprintf(fs, "\n");
    }
    /* Close files. */
    fclose(fo);
    fclose(fs);
  }

  for(i=0; i<Map->NYfine; i++) { 
    free(failure[i]);
    free(SedThickness[i]);
    free(InitialSediment[i]);
  }
  free(failure);
  free(SedThickness);
  free(InitialSediment);
  free(SegmentSediment);
  free(SedDiams);
  free(InitialSegmentSediment);
}

/*****************************************************************************
  End of Main
*****************************************************************************/

  void enqueue(node **head, node **tail, int y, int x)
{
  node *new;

  // Allocate and initialize a new node
  new = (node *) malloc(sizeof(node));
  new->x = x;
  new->y = y;
  new->next = NULL;

  if(empty(*head)) {

    // If *head is empty, the queue is empty and we're inserting the first node;
    // therefore this node is both the header and the tail
    *head = new;
    *tail = new;

  }
  else {

    //    if(!empty(*tail)) {
    //   fprintf(stderr,"New node is not at end of queue\n");
    //  exit(0);
    //   }

    // Point the tail node's "next" to the new node
    (*tail)->next = new;

    // Now this new node is the tail, so point "tail" to this new node
    *tail = new;
  }

}

void dequeue(node **head, node **tail, int *y, int *x)
{

  node *temp;

  //  if(!empty(*head))
  //    {
  //      fprintf(stderr,"Node is not at head of queue\n");
  //      exit(0);
  //      }

  *y = (*head)->y;
  *x = (*head)->x;

  // Point temp to the header node so we still have a reference to it
  temp = *head;

  // Point *head to the next node; this is the new header node
  *head = (*head)->next;
  if(head == NULL) tail = NULL;

  // De-allocate the old header node, now that we're no longer using it
  free(temp);

}


