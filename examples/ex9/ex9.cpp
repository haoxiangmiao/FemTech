#include "FemTech.h"
#include "blas.h"

#include <assert.h>


/*Delare Functions*/
void ApplyBoundaryConditions(double dMax, double tMax);
void CustomPlot();

/* Global Variables/Parameters  - could be moved to parameters.h file?  */
int nPlotSteps = 50;
double Time, dt;
int nSteps;
bool ImplicitStatic = false;
bool ImplicitDynamic = false;
bool ExplicitDynamic = true;
double ExplicitTimeStepReduction = 0.8;
double FailureTimeStep = 1e-11;

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
  stepTime[plot_counter] = Time;
  CustomPlot();

  if (ImplicitStatic) {
    // Static solution
    double dMax = 0.1; // max displacment in meters
    double tMax = 1.0;
    ShapeFunctions();
    CreateLinearElasticityCMatrix();
    ApplyBoundaryConditions(dMax, tMax);
    Assembly((char *)"stiffness");
    ApplySteadyBoundaryConditions();
    SolveSteadyImplicit();
    Time = tMax;
    /* Write final, deformed configuration*/
    WriteVTU(argv[1], 1);
  } else if (ImplicitDynamic) {
    // Dynamic Implicit solution using Newmark's scheme for time integration
    dt = 0.1;
    double tMax = 1.0;
    double dMax = 0.1; // max displacment in meters
    ShapeFunctions();
    CreateLinearElasticityCMatrix();
    Time = 1.0;
    ApplyBoundaryConditions(dMax, tMax);
    Assembly((char *)"stiffness");
    Assembly((char *)"mass");
    /* beta and gamma of Newmark's scheme */
    double beta = 0.25;
    double gamma = 0.5;
    SolveUnsteadyNewmarkImplicit(beta, gamma, dt, tMax, argv[1]);
  } else if (ExplicitDynamic) {
    // Dynamic Explcit solution using....

    dt = 0.0;
    double tMax = 1.00; // max simulation time in seconds
    double dMax = 0.007; // max displacment in meters

    int time_step_counter = 0;
    /** Central Difference Method - Beta and Gamma */
    // double beta = 0;
    // double gamma = 0.5;

    ShapeFunctions();
    /*  Step-1: Calculate the mass matrix similar to that of belytschko. */
    AssembleLumpedMass();

    // Used if initial velocity and acceleration BC is to be set.
    ApplyBoundaryConditions(dMax, tMax);

    /* Obtain dt, according to Belytschko dt is calculated at end of getForce */
    dt = ExplicitTimeStepReduction * StableTimeStep();
    /* Step-2: getforce step from Belytschko */
    GetForce(); // Calculating the force term.

    /* Step-3: Calculate accelerations */
    CalculateAccelerations();

    nSteps = (int)(tMax / dt);
    int nsteps_plot = (int)(nSteps / nPlotSteps);

    if (world_rank == 0) {
      printf("inital dt = %3.3e, nSteps = %d, nsteps_plot = %d\n", dt, nSteps,
            nsteps_plot);
    }

    time_step_counter = time_step_counter + 1;
    double t_n = 0.0;

    if (world_rank == 0) {
      printf(
          "------------------------------- Loop ----------------------------\n");
      printf("Time : %f, tmax : %f\n", Time, tMax);
    }

    /* Step-4: Time loop starts....*/
    while (Time < tMax) {
      t_n = Time;
      double t_np1 = Time + dt;
      Time = t_np1; /*Update the time by adding full time step */
      if (world_rank == 0) {
        printf("Time : %15.6e, dt=%15.6e, tmax : %15.6e\n", Time, dt, tMax);
      }
      double dt_nphalf = dt;                 // equ 6.2.1
      double t_nphalf = 0.5 * (t_np1 + t_n); // equ 6.2.1

      /* Step 5 from Belytschko Box 6.1 - Update velocity */
      for (int i = 0; i < nDOF; i++) {
        if (boundary[i]) {
          velocities_half[i] = velocities[i];
        } else {
          velocities_half[i] =
              velocities[i] + (t_nphalf - t_n) * accelerations[i];
        }
      }

      // Store old displacements and accelerations for energy computation
      memcpy(displacements_prev, displacements, nDOF * sizeof(double));
      memcpy(accelerations_prev, accelerations, nDOF * sizeof(double));
      // Store internal external force from previous step to compute energy
      memcpy(fi_prev, fi, nDOF * sizeof(double));
      memcpy(fe_prev, fe, nDOF * sizeof(double));

      for (int i = 0; i < nDOF; i++) {
        if (!boundary[i]) {
          displacements[i] = displacements[i] + dt_nphalf * velocities_half[i];
        }
      }
      /* Step 6 Enforce displacement boundary Conditions */
      ApplyBoundaryConditions(dMax, tMax);

      /* Step - 8 from Belytschko Box 6.1 - Calculate net nodal force*/
      GetForce(); // Calculating the force term.
      /* Step - 9 from Belytschko Box 6.1 - Calculate Accelerations */
      CalculateAccelerations(); // Calculating the new accelerations from total
                                // nodal forces.

      /** Step- 10 - Second Partial Update of Nodal Velocities */
      for (int i = 0; i < nDOF; i++) {
        if (!boundary[i]) {
          velocities[i] =
              velocities_half[i] + (t_np1 - t_nphalf) * accelerations[i];
        }
      }

      /** Step - 11 Checking* Energy Balance */
      int writeFlag = time_step_counter%nsteps_plot;
      CheckEnergy(Time, writeFlag);

      if (writeFlag == 0) {
        plot_counter = plot_counter + 1;
        CalculateStrain();
        printf("------Plot %d: WriteVTU by rank : %d\n", plot_counter, world_rank);
        WriteVTU(argv[1], plot_counter);
        if (plot_counter < MAXPLOTSTEPS) {
          stepTime[plot_counter] = Time;
          WritePVD(argv[1], plot_counter);
        }
        CustomPlot();

#ifdef DEBUG
        if (debug) {
          printf("DEBUG : Printing Displacement Solution\n");
          for (int i = 0; i < nNodes; ++i) {
            for (int j = 0; j < ndim; ++j) {
              printf("%15.6E", displacements[i * ndim + j]);
            }
            printf("\n");
          }
        }
#endif // DEBUG
      }
      time_step_counter = time_step_counter + 1;
      dt = ExplicitTimeStepReduction * StableTimeStep();
      // Barrier not a must
      MPI_Barrier(MPI_COMM_WORLD);
    } // end explcit while loop

    // Write out the last time step
    CustomPlot();
  } // end if ExplicitDynamic
#ifdef DEBUG
  if (debug) {
    printf("DEBUG : Printing Displacement Solution\n");
    for (int i = 0; i < nNodes; ++i) {
      for (int j = 0; j < ndim; ++j) {
        printf("%15.6E", displacements[i * ndim + j]);
      }
      printf("\n");
    }
  }
#endif // DEBUG

  /* Below are things to do at end of program */
  // if (world_rank == 0) {
  //   if (plot_counter < MAXPLOTSTEPS) {
  //     stepTime[plot_counter] = Time;
  //     WritePVD(argv[1], plot_counter);
  //   }
  // }
  FreeArrays();
  MPI_Finalize();
  return 0;
}

void ApplyBoundaryConditions(double dMax, double tMax) {
  double tol = 1e-5;
  int count = 0;
  double AppliedDisp;

  // Apply Ramped Displacment
  if (ExplicitDynamic || ImplicitDynamic) {
    AppliedDisp = Time * (dMax / tMax);
  } else if (ImplicitStatic) {
    AppliedDisp = dMax;
  }
  int index;

  for (int i = 0; i < nNodes; i++) {
    // if x value = 0, constrain node to x plane (0-direction)
    index = ndim * i + 0;
    if (fabs(coordinates[index] - 0.0) < tol) {
      boundary[index] = 1;
      displacements[index] = 0.0;
      velocities[index] = 0.0;
      // For energy computations
      accelerations[index] = 0.0;
      count = count + 1;
    }
    // if y coordinate = 0, constrain node to y plane (1-direction)
    index = ndim * i + 1;
    if (fabs(coordinates[index] - 0.0) < tol) {
      boundary[index] = 1;
      displacements[index] = 0.0;
      velocities[index] = 0.0;
      accelerations[index] = 0.0;
      count = count + 1;
    }
    // if z coordinate = 0, constrain node to z plane (2-direction)
    index = ndim * i + 2;
    if (fabs(coordinates[index] - 0.0) < tol) {
      boundary[index] = 1;
      displacements[index] = 0.0;
      velocities[index] = 0.0;
      accelerations[index] = 0.0;
      count = count + 1;
    }
    // if y coordinate = 1, apply disp. to node = 0.1 (1-direction)
    index = ndim * i + 1;
    if (fabs(coordinates[index] - 0.005) < tol) {
      boundary[index] = 1;
      count = count + 1;
      // note that this may have to be divided into
      // diplacement increments for both implicit and
      // explicit solver. In the future this would be
      // equal to some time dependent function i.e.,
      // CalculateDisplacement to get current increment out
      //  displacment to be applied.
      displacements[index] = AppliedDisp;
      velocities[index] = dMax/tMax;
      // For energy computations
      accelerations[index] = 0.0;
    }
  }
  if (world_rank == 0) {
    printf("Applied Disp = %10.5e\n", AppliedDisp);
  }
  return;
}

void CustomPlot() {
  double tol = 1e-5;
  FILE *datFile;
  int x = 0;
  int y = 1;
  int z = 2;

  if (fabs(Time - 0.0) < 1e-16) {
    datFile = fopen("plot.dat", "w");
    fprintf(datFile, "# Results for Node ?\n");
    fprintf(datFile, "# Time  DispX    DispY   DispZ\n");
    fprintf(datFile, "%11.5e %11.5e  %11.5e  %11.5e\n", 0.0, 0.0, 0.0, 0.0);

  } else {
    datFile = fopen("plot.dat", "a");
    for (int i = 0; i < nNodes; i++) {
      if (fabs(coordinates[ndim * i + x] - 0.005) < tol &&
          fabs(coordinates[ndim * i + y] - 0.005) < tol &&
          fabs(coordinates[ndim * i + z] - 0.005) < tol) {

        fprintf(datFile, "%11.5e %11.5e  %11.5e  %11.5e\n", Time,
                displacements[ndim * i + x], displacements[ndim * i + y],
                displacements[ndim * i + z]);
      }
    }
  }

  fclose(datFile);
  return;
}
