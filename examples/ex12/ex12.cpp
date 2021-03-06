#include "FemTech.h"
#include "blas.h"

/*Delare Functions*/
void ApplyBoundaryConditions(double dMax, double tMax);

/* Global Variables/Parameters  - could be moved to parameters.h file?  */
double Time, dt;
int nSteps;
bool ImplicitStatic = false;
bool ImplicitDynamic = false;
bool ExplicitDynamic = true;
double ExplicitTimeStepReduction = 0.8;
double FailureTimeStep = 1e-11;
int nPlotSteps = 50;

int main(int argc, char **argv) {

  // Initialize the MPI environment
  MPI_Init(NULL, NULL);
  // Get the number of processes
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);
  // Get the rank of the process
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);

  ReadInputFile(argv[1]);
	ReadMaterials();

  PartitionMesh();

  AllocateArrays();

  /* Write inital, undeformed configuration*/
  Time = 0.0;
  int plot_counter = 0;
  WriteVTU(argv[1], plot_counter);

  // Dynamic Explcit solution using....
  double tMax = 1; // max simulation time in seconds
  double dMax = 0.5;  // max displacment in meters
  int time_step_counter = 0;
  /** Central Difference Method - Beta and Gamma */
  // double beta = 0;
  // double gamma = 0.5;

  ShapeFunctions();
  /*  Step-1: Calculate the mass matrix similar to that of belytschko. */
  AssembleLumpedMass();

  /* obtain dt, according to Belytschko dt is calculated at end of getForce */
  dt = ExplicitTimeStepReduction * StableTimeStep();
  /* Step-2: getforce step from Belytschko */
  GetForce(); // Calculating the force term.

  tMax = 2*dt;

  /* Step-3: Calculate accelerations */
  CalculateAccelerations();

  nSteps = (int)(tMax / dt);
  int nsteps_plot = (int)(nSteps / nPlotSteps);
  printf("inital dt = %3.3e, nSteps = %d, nsteps_plot = %d\n", dt, nSteps,
          nsteps_plot);

  // Save old displacements
  // memcpy(displacements_prev, displacements, nDOF*sizeof(double));

  /* Step-4: Time loop starts....*/
  time_step_counter = time_step_counter + 1;
  double t_n = 0.0;
  while (Time <= tMax) {
    /*Step 4 */
    // note: box 6.1 in belytschko
    // varibles t_np1 = t_n+1
    // dt_nphalf = deltat_n+1/2

    t_n = Time;
    double t_np1 = Time + dt;
    Time = t_np1; /*Update the time by adding full time step */
    double dt_nphalf = dt;        // equ 6.2.1
    double t_nphalf = 0.5 * (t_np1 + t_n); // equ 6.2.1

    /* Step 5 from Belytschko Box 6.1 - Update velocity */
    for (int i = 0; i < nDOF; i++) {
      velocities_half[i] =
          velocities[i] + (t_nphalf - t_n) * accelerations[i];
    }

    /* Update Nodal Displacements */
    printf("%d (%.6f) Dispalcements\n--------------------\n",
            time_step_counter, Time);
    for (int i = 0; i < nDOF; i++) {
      displacements[i] = displacements[i] + dt_nphalf * velocities_half[i];
       printf("%.6f, %0.6f, %0.6f\n", displacements[i], velocities[i],
       accelerations[i]);
    }
    /* Step 6 Enfotce velocity boundary Conditions */
    ApplyBoundaryConditions(dMax, tMax);

    /* Step - 8 from Belytschko Box 6.1 - Calculate net nodal force*/
    GetForce(); // Calculating the force term.

    /* Step - 9 from Belytschko Box 6.1 - Calculate Accelerations */
    CalculateAccelerations(); // Calculating the new accelerations from total
                              // nodal forces.

    /** Step- 10 - Second Partial Update of Nodal Velocities */
    for (int i = 0; i < nDOF; i++) {
      velocities[i] =
          velocities_half[i] + (t_np1 - t_nphalf) * accelerations[i];
    }

    /** Step - 11 Checking* Energy Balance */
    int writeFlag = time_step_counter%nsteps_plot;
    CheckEnergy(Time, writeFlag);

    if (writeFlag == 0) {
      plot_counter = plot_counter + 1;
      printf("------Plot %d: WriteVTU\n", plot_counter);
      WriteVTU(argv[1], plot_counter);
      if (debug) {
        printf("DEBUG : Printing Displacement Solution\n");
        for (int i = 0; i < nNodes; ++i) {
          for (int j = 0; j < ndim; ++j) {
            printf("%12.4f", displacements[i*ndim+j]);
          }
          printf("\n");
        }
      }
    }
    time_step_counter = time_step_counter + 1;

    // not sure if this is needed. part of getForce in Belytschko
    // dt = ExplicitTimeStepReduction * StableTimeStep();

  } // end explcit while loop

  if (debug) {
    printf("DEBUG : Printing Displacement Solution\n");
    for (int i = 0; i < nNodes; ++i) {
      for (int j = 0; j < ndim; ++j) {
        printf("%12.4f", displacements[i*ndim+j]);
      }
      printf("\n");
    }
  }

  /* Below are things to do at end of program */
  if (world_rank == 0) {
    WritePVD(argv[1], plot_counter);
  }
  FreeArrays();
  MPI_Finalize();
  return 0;
}

void ApplyBoundaryConditions(double dMax, double tMax) {
  int count = 0;
  double k;
	double tol = 1e-5;

  // Apply Ramped Displacment
  if (ExplicitDynamic || ImplicitDynamic) {
    k = Time * (dMax / tMax);
  } else if (ImplicitStatic) {
    k = dMax;
  }
  printf("Applied displacement = %.6f\n", k);

  for (int i = 0; i < nNodes; i++) {

		if (fabs(coordinates[ndim * i + 0] - 0.0) < tol) {
			boundary[ndim * i + 0] = 1;
			displacements[ndim * i + 0] = 0.0;
			count = count + 1;
		}

		if (fabs(coordinates[ndim * i + 0] - 1.0) < tol) {
			boundary[ndim * i + 0] = 1;
			displacements[ndim * i + 0] += k;
			printf("app disp = %3.3f\n",displacements[ndim * i + 0]);
			count = count + 1;
		}

		// constrain in y
    boundary[ndim * i + 1] = 1;
    displacements[ndim * i + 1] = 0.0;
    count = count + 1;

    	// constrain in z
    boundary[ndim * i + 2] = 1;
    displacements[ndim * i + 2] = 0.0;
    count = count + 1;
  }
  return;
}
