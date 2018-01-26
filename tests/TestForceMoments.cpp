
//*************************************************************************
// Lattice Boltzmann Simulator for Single Phase Flow in Porous Media
// James E. McCLure
//*************************************************************************
#include <stdio.h>
#include <iostream>
#include <fstream>
#include "common/ScaLBL.h"
#include "common/MPI_Helpers.h"

using namespace std;



extern void PrintNeighborList(int * neighborList, int Np, int rank) {
	if (rank == 0) {
		int n;
		int neighbor;

		for (int i = 0; i < Np; i++) {
			printf("idx=%d: ",i);
			for (int l = 0; l < 10; l++) {   // was 18
				neighbor = neighborList[l*Np + i];
				printf("%d ",neighbor);
			}
			printf("\n");
		}
		printf("\n\n");
	}
}

//***************************************************************************************
int main(int argc, char **argv)
{
	//*****************************************
	// ***** MPI STUFF ****************
	//*****************************************
	// Initialize MPI
	int rank,nprocs;
	MPI_Init(&argc,&argv);
	MPI_Comm comm = MPI_COMM_WORLD;
	MPI_Comm_rank(comm,&rank);
	MPI_Comm_size(comm,&nprocs);
	int check;
	{
		// parallel domain size (# of sub-domains)
		int nprocx,nprocy,nprocz;
		int iproc,jproc,kproc;


		if (rank == 0){
			printf("********************************************************\n");
			printf("Running Unit Test: TestForceMoments	\n");
			printf("********************************************************\n");
		}

		// BGK Model parameters
		string FILENAME;
		unsigned int nBlocks, nthreads;
		int timestepMax, interval;
		double tau,Fx,Fy,Fz,tol;
		// Domain variables
		double Lx,Ly,Lz;
		int nspheres;
		int Nx,Ny,Nz;
		int i,j,k,n;
		int dim = 3; if (rank == 0) printf("dim=%d\n",dim);
		int timestep = 0;
		int timesteps = 2;

		tau =1.0;
		double mu=(tau-0.5)/3.0;
		double rlx_setA=1.0/tau;
		double rlx_setB = 8.f*(2.f-rlx_setA)/(8.f-rlx_setA);
		Fx = Fy = 1.0;
		Fz = 1.0;

		if (rank==0){
			//.......................................................................
			// Reading the domain information file
			//.......................................................................
			ifstream domain("Domain.in");
			if (domain.good()){
				printf("domain.good == true \n");
				domain >> nprocx;
				domain >> nprocy;
				domain >> nprocz;
				domain >> Nx;
				domain >> Ny;
				domain >> Nz;
				domain >> nspheres;
				domain >> Lx;
				domain >> Ly;
				domain >> Lz;
			}
			else if (nprocs==1){
				nprocx=nprocy=nprocz=1;
				Nx=3; Ny = 3;
				Nz = 3;
				nspheres=0;
				Lx=Ly=Lz=1;
			}
			//.......................................................................
		}
		// **************************************************************
		// Broadcast simulation parameters from rank 0 to all other procs
		MPI_Barrier(comm);
		//.................................................
		MPI_Bcast(&Nx,1,MPI_INT,0,comm);
		MPI_Bcast(&Ny,1,MPI_INT,0,comm);
		MPI_Bcast(&Nz,1,MPI_INT,0,comm);
		MPI_Bcast(&nprocx,1,MPI_INT,0,comm);
		MPI_Bcast(&nprocy,1,MPI_INT,0,comm);
		MPI_Bcast(&nprocz,1,MPI_INT,0,comm);
		MPI_Bcast(&nspheres,1,MPI_INT,0,comm);
		MPI_Bcast(&Lx,1,MPI_DOUBLE,0,comm);
		MPI_Bcast(&Ly,1,MPI_DOUBLE,0,comm);
		MPI_Bcast(&Lz,1,MPI_DOUBLE,0,comm);
		//.................................................
		MPI_Barrier(comm);
		// **************************************************************
		// **************************************************************

		if (nprocs != nprocx*nprocy*nprocz){
			printf("nprocx =  %i \n",nprocx);
			printf("nprocy =  %i \n",nprocy);
			printf("nprocz =  %i \n",nprocz);
			INSIST(nprocs == nprocx*nprocy*nprocz,"Fatal error in processor count!");
		}

		if (rank==0){
			printf("********************************************************\n");
			printf("Sub-domain size = %i x %i x %i\n",Nx,Ny,Nz);
			printf("********************************************************\n");
		}

		MPI_Barrier(comm);
		kproc = rank/(nprocx*nprocy);
		jproc = (rank-nprocx*nprocy*kproc)/nprocx;
		iproc = rank-nprocx*nprocy*kproc-nprocz*jproc;

		if (rank == 0) {
			printf("i,j,k proc=%d %d %d \n",iproc,jproc,kproc);
		}
		MPI_Barrier(comm);
		if (rank == 1){
			printf("i,j,k proc=%d %d %d \n",iproc,jproc,kproc);
			printf("\n\n");
		}

		double iVol_global = 1.0/Nx/Ny/Nz/nprocx/nprocy/nprocz;
		int BoundaryCondition=0;

		Domain Dm(Nx,Ny,Nz,rank,nprocx,nprocy,nprocz,Lx,Ly,Lz,BoundaryCondition);

		Nx += 2;
		Ny += 2;
		Nz += 2;
		int N = Nx*Ny*Nz;

		//.......................................................................
		// Assign the phase ID field
		//.......................................................................
		char LocalRankString[8];
		sprintf(LocalRankString,"%05d",rank);
		char LocalRankFilename[40];
		sprintf(LocalRankFilename,"ID.%05i",rank);
		/*
		FILE *IDFILE = fopen(LocalRankFilename,"rb");
		if (IDFILE==NULL) ERROR("Error opening file: ID.xxxxx");
		fread(Dm.id,1,N,IDFILE);
		fclose(IDFILE);
		 */

		// initialize empty domain
		for (k=0;k<Nz;k++){
			for (j=0;j<Ny;j++){
				for (i=0;i<Nx;i++){
					n = k*Nx*Ny+j*Nx+i;
					Dm.id[n]=1;
				}
			}
		}
		Dm.CommInit(comm);
		MPI_Barrier(comm);
		if (rank == 0) cout << "Domain set." << endl;

		int Np=0;  // number of local pore nodes
		for (k=1;k<Nz-1;k++){
			for (j=1;j<Ny-1;j++){
				for (i=1;i<Nx-1;i++){
					n = k*Nx*Ny+j*Nx+i;
					if (Dm.id[n] > 0){
						Np++;
					}
				}
			}
		}

		if (rank==0)	printf ("Create ScaLBL_Communicator \n");

		// Create a communicator for the device
		ScaLBL_Communicator ScaLBL_Comm(Dm);

		//...........device phase ID.................................................
		if (rank==0)	printf ("Copying phase ID to device \n");
		char *ID;
		ScaLBL_AllocateDeviceMemory((void **) &ID, N);						// Allocate device memory
		// Copy to the device
		ScaLBL_CopyToDevice(ID, Dm.id, N);
		//...........................................................................

		if (rank==0){
			printf("Total domain size = %i \n",N);
			printf("Reduced domain size = %i \n",Np);
		}


		// LBM variables
		if (rank==0)	printf ("Allocating distributions \n");

		int neighborSize=18*Np*sizeof(int);
		int *neighborList;
		IntArray Map(Nx,Ny,Nz);

		neighborList= new int[18*Np];
		ScaLBL_Comm.MemoryOptimizedLayoutAA(Map,neighborList,Dm.id,Np);


	        if (rank == 0) PrintNeighborList(neighborList,Np, rank);

		MPI_Barrier(comm);

		//......................device distributions.................................
		int dist_mem_size = Np*sizeof(double);

		int *NeighborList;
		//		double *f_even,*f_odd;
		double * dist;
		double * Velocity;
		//...........................................................................
		ScaLBL_AllocateDeviceMemory((void **) &dist, 19*dist_mem_size);
		ScaLBL_AllocateDeviceMemory((void **) &NeighborList, neighborSize);
		ScaLBL_AllocateDeviceMemory((void **) &Velocity, 3*sizeof(double)*Np);
		ScaLBL_CopyToDevice(NeighborList,     neighborList, neighborSize);
		//...........................................................................

		/*
		 *  AA Algorithm begins here
		 *
		 */
		ScaLBL_D3Q19_Init(dist, Np);

		//.......create and start timer............
		double starttime,stoptime,cputime;

		ScaLBL_DeviceBarrier(); MPI_Barrier(comm);
		starttime = MPI_Wtime();

		/************ MAIN ITERATION LOOP (timing communications)***************************************/
		//ScaLBL_Comm.SendD3Q19(dist, &dist[10*Np]);
		//ScaLBL_Comm.RecvD3Q19(dist, &dist[10*Np]);
		ScaLBL_DeviceBarrier(); MPI_Barrier(comm);

		if (rank==0) printf("Beginning AA timesteps...\n");
		if (rank==0) printf("********************************************************\n");
		if (rank==0) printf("No. of timesteps for timing: %i \n", timesteps);

		while (timestep < 2) {

		  ScaLBL_D3Q19_AAeven_MRT(dist, 0, Np, Np, rlx_setA, rlx_setB, Fx, Fy, Fz);
			ScaLBL_Comm.SendD3Q19AA(dist); //READ FROM NORMAL
			ScaLBL_Comm.RecvD3Q19AA(dist); //WRITE INTO OPPOSITE
			ScaLBL_DeviceBarrier(); MPI_Barrier(comm);
			timestep++;

			ScaLBL_D3Q19_AAodd_MRT(NeighborList, dist, 0, Np, Np, rlx_setA, rlx_setB, Fx, Fy, Fz);
			ScaLBL_Comm.SendD3Q19AA(dist); //READ FROM NORMAL
			ScaLBL_Comm.RecvD3Q19AA(dist); //WRITE INTO OPPOSITE
			ScaLBL_DeviceBarrier(); MPI_Barrier(comm);
			
			timestep++;

		}


		//************************************************************************/
		stoptime = MPI_Wtime();
		//	cout << "CPU time: " << (stoptime - starttime) << " seconds" << endl;
		cputime = stoptime - starttime;
		//	cout << "Lattice update rate: "<< double(Nx*Ny*Nz*timestep)/cputime/1000000 <<  " MLUPS" << endl;
		double MLUPS = double(Np*timestep)/cputime/1000000;
		if (rank==0) printf("********************************************************\n");
		if (rank==0) printf("CPU time = %f \n", cputime);
		if (rank==0) printf("Lattice update rate (per process)= %f MLUPS \n", MLUPS);
		MLUPS *= nprocs;
		if (rank==0) printf("Lattice update rate (process)= %f MLUPS \n", MLUPS);
		if (rank==0) printf("********************************************************\n");

		// Number of memory references from the swap algorithm (per timestep)
		// 18 reads and 18 writes for each lattice site
		double MemoryRefs = Np*38;

		int SIZE=Np*sizeof(double);
		/*
		double *Vz;
		Vz= new double [Np];
		double *Vx;
		Vx= new double [Np];
		double *Vy;
		Vy= new double [Np];
		ScaLBL_D3Q19_AA_Velocity(dist, &dist[10*Np],Velocity, Np);
		ScaLBL_DeviceBarrier(); MPI_Barrier(comm);
		ScaLBL_CopyToHost(&Vx[0],&Velocity[0],SIZE);
		ScaLBL_CopyToHost(&Vy[0],&Velocity[Np],SIZE);
		ScaLBL_CopyToHost(&Vz[0],&Velocity[2*Np],SIZE);

		printf("Force: %f,%f,%f \n",Fx,Fy,Fz);
		double vz;
		double W = 1.f*Nx-4;
		j=Ny/2; k=Nz/2;
		for (j=1;j<Ny-1;j++){
			for (i=1;i<Nx-1;i++){
				n = k*Nx*Ny+j*Nx+i;
				//printf("%i ",Dm.id[n]);
				n = Map(i,j,k);
				//printf("%i,%i,%i; %i :",i,j,k,n);
				if (n<0) vz =0.f;
				else vz=Vz[n];
				printf("%f ",vz);
			}
			printf("\n");
		}
		*/
		
		double *DIST;
		DIST= new double [19*Np];
		ScaLBL_CopyToHost(&DIST[0],&dist[0],19*SIZE);
		
		i=Nx/2;
		printf("x = constant \n");
		for (int q=0; q<9; q++){
			int a = 2*q+1;
			int b = 2*(q+1);
			printf("************* \n");
			printf("print slice for distribution pair %i,%i \n",a,b);
			for (k=1;k<Nz-1;k++){
				for (j=1;j<Ny-1;j++){
					n = k*Nx*Ny+j*Nx+i;
					//printf("%i ",Dm.id[n]);
					n = Map(i,j,k);
					double fa = DIST[(2*q+1)*Np+n];
					double fb = DIST[2*(q+1)*Np+n];
					printf("%f,%f  ",fa,fb);
				}
				printf("\n");
			}
			printf("************* \n");
		}

		printf("y = constant \n");
		j=Ny/2;
		for (int q=0; q<9; q++){
			int a = 2*q+1;
			int b = 2*(q+1);
			printf("************* \n");
			printf("print slice for distribution pair %i,%i \n",a,b);
			for (k=1;k<Nz-1;k++){
				for (i=1;i<Nx-1;i++){
					n = k*Nx*Ny+j*Nx+i;
					//printf("%i ",Dm.id[n]);
					n = Map(i,j,k);
					double fa = DIST[(2*q+1)*Np+n];
					double fb = DIST[2*(q+1)*Np+n];
					printf("%f,%f  ",fa,fb);
				}
				printf("\n");
			}
			printf("************* \n");
		}
		k=Nz/2;
		printf("z = constant \n");
		for (int q=0; q<9; q++){
			int a = 2*q+1;
			int b = 2*(q+1);
			printf("************* \n");
			printf("print slice for distribution pair %i,%i \n",a,b);
			for (j=1;j<Ny-1;j++){
				for (i=1;i<Nx-1;i++){
					n = k*Nx*Ny+j*Nx+i;
					//printf("%i ",Dm.id[n]);
					n = Map(i,j,k);
					double fa = DIST[(2*q+1)*Np+n];
					double fb = DIST[2*(q+1)*Np+n];
					printf("%f,%f  ",fa,fb);
				}
				printf("\n");
			}
			printf("************* \n");
		}



	}
	// ****************************************************
	MPI_Barrier(comm);
	MPI_Finalize();
	// ****************************************************

	return check;
}