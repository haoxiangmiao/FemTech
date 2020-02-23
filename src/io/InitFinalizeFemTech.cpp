#include "FemTech.h"

#include "mpi.h"

#include <string>
#include <ctime>

int world_rank;
int world_size;

std::string InitFemTech(int argc, char **argv) {
  // Initialize the MPI environment
  MPI_Init(NULL, NULL);
  // Get the number of processes
  MPI_Comm_size(MPI_COMM_WORLD, &world_size);
  // Get the rank of the process
  MPI_Comm_rank(MPI_COMM_WORLD, &world_rank);
  if (argc < 1) {
    fprintf(stdout, "ERROR(%d): Please provide an input JSON file\n", world_rank);
    fprintf(stdout, "ERROR(%d): Usage %s inputFile.json\n", world_rank, argv[0]);
    MPI_Abort(MPI_COMM_WORLD, EXIT_FAILURE);
  }
  // create simulation unique id from time
  time_t t = time(0);   // get time now
  struct tm * now = localtime( & t );
  char buffer [80];
  strftime(buffer, 80, "%d_%m_%Y_%H_%M_%S", now);
  std::string uid(buffer);
  std::string logFile = "femtech_"+uid+".log";

  // Initialize the output log file
  initLog(logFile.c_str());

  return uid;
}

void FinalizeFemTech() {
  finaliseLog();
  FreeArrays();
  MPI_Finalize();
}

void TerminateFemTech(int errorCode) {
  finaliseLog();
  FreeArrays();
  MPI_Abort(MPI_COMM_WORLD, errorCode);
}
