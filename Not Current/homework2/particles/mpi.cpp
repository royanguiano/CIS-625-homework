#include <mpi.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include "common.h"
#include <vector>
using namespace std;

#if !defined density
#define density 0.0005
#endif

#if !defined mass
#define mass 0.01
#endif

#if !defined cutoff
#define cutoff 0.01
#endif

#if !defined min_r
#define min_r (cutoff/100)
#endif

#if !defined dt
#define dt      0.0005
#endif


// calculate particle's bin number
int calculateBinNum(particle_t &p, int binsPerSide)
{
    return ( floor(p.x/cutoff) + binsPerSide*floor(p.y/cutoff) );
}

// changed
//
// particle data structure
//
struct particleNode
{
    particle_t *particle;
    int gridX, gridY;
    int index;
    struct particleNode *next, *prev;
};
typedef struct particleNode ParticleNode;

//My updated force function that updates both particles at the same time
void static inline apply_forceBoth( particle_t &particle, particle_t &neighbor , double *dmin, double *davg, int *navg)
{
    
    double dx = neighbor.x - particle.x;
    double dy = neighbor.y - particle.y;
    double r2 = dx * dx + dy * dy;
    if( r2 > cutoff*cutoff )
        return;
    if (r2 != 0)
    {
        if (r2/(cutoff*cutoff) < *dmin * (*dmin))
            *dmin = sqrt(r2)/cutoff;
        (*davg) += sqrt(r2)/cutoff;
        (*navg) ++;
    }
    
    r2 = fmax( r2, min_r*min_r );
    double r = sqrt( r2 );
    
    //
    //  very simple short-range repulsive force
    //
    double coef = ( 1 - cutoff / r ) / r2 / mass;
    
    double coefdx = coef * dx;
    double coefdy = coef * dy;
    
    particle.ax += coefdx;
    particle.ay += coefdy;
    
    neighbor.ax -= coefdx;
    neighbor.ay -= coefdy;
}


//
//  benchmarking program
//
int main( int argc, char **argv )
{    
    int navg, nabsavg=0;
    double dmin, absmin=1.0,davg,absavg=0.0;
    double rdavg,rdmin;
    int rnavg; 
 
    //
    //  process command line parameters
    //
    if( find_option( argc, argv, "-h" ) >= 0 )
    {
        printf( "Options:\n" );
        printf( "-h to see this help\n" );
        printf( "-n <int> to set the number of particles\n" );
        printf( "-o <filename> to specify the output file name\n" );
        printf( "-s <filename> to specify a summary file name\n" );
        printf( "-no turns off all correctness checks and particle output\n");
        return 0;
    }
    
    int n = read_int( argc, argv, "-n", 1000 );
    char *savename = read_string( argc, argv, "-o", NULL );
    char *sumname = read_string( argc, argv, "-s", NULL );
    
    //
    //  set up MPI
    //
    int n_proc, rank;
    MPI_Init( &argc, &argv );
    MPI_Comm_size( MPI_COMM_WORLD, &n_proc );
    MPI_Comm_rank( MPI_COMM_WORLD, &rank );
    
    //
    //  allocate generic resources
    //
    FILE *fsave = savename && rank == 0 ? fopen( savename, "w" ) : NULL;
    FILE *fsum = sumname && rank == 0 ? fopen ( sumname, "a" ) : NULL;


    particle_t *particles = (particle_t*) malloc( n * sizeof(particle_t) );
    
    MPI_Datatype PARTICLE;
    MPI_Type_contiguous( 6, MPI_DOUBLE, &PARTICLE );
    MPI_Type_commit( &PARTICLE );
    
    //
    //  set up the data partitioning across processors
    //
    int particle_per_proc = (n + n_proc - 1) / n_proc;
    int *partition_offsets = (int*) malloc( (n_proc+1) * sizeof(int) );
    for( int i = 0; i < n_proc+1; i++ )
        partition_offsets[i] = min( i * particle_per_proc, n );
    
    int *partition_sizes = (int*) malloc( n_proc * sizeof(int) );
    for( int i = 0; i < n_proc; i++ )
        partition_sizes[i] = partition_offsets[i+1] - partition_offsets[i];
    
    //
    //  allocate storage for local partition
    //
    int nlocal = partition_sizes[rank];
    particle_t *local = (particle_t*) malloc( nlocal * sizeof(particle_t) );
    
    //
    //  initialize and distribute the particles (that's fine to leave it unoptimized)
    //
    set_size( n );
    if( rank == 0 )
        init_particles( n, particles );
    
    
    
    
    // create spatial bins (of size cutoff by cutoff)
    double size = sqrt( density*n );
    int binsPerSide = ceil(size/cutoff);
    int numbins = binsPerSide*binsPerSide;
    vector<particle_t*> *bins = new vector<particle_t*>[numbins];
    
    //Adding code
    /*
    //Create the field grid
    double blockSize = cutoff; //Use to be cutoff
    int searchArea = 2;//33
    double fieldSize = sqrt(density * n);
    int gridSize = ceil(fieldSize / blockSize);
    //Create particle grid
    ParticleNode ***grid = (ParticleNode ***)malloc(gridSize * sizeof(ParticleNode **));
    for (int i=0; i<gridSize; i++)
        grid[i] = (ParticleNode **)malloc(gridSize * sizeof(ParticleNode*));
    //Create particle node array
    ParticleNode **particleNodes = (ParticleNode**) malloc( n * sizeof(ParticleNode*) );
    
    //Clear the particle grid
    for (int x = 0; x < gridSize; x++) {
        for (int y = 0; y < gridSize; y++) {
            grid[x][y] = NULL;
        }
    }
    
    //Sort particles in linked lists in the grid
    for (int i = 0; i < n; i++) {
        particle_t *particle = &particles[i];
        //Get location in grid
        int locationX = floor(particle->x / blockSize);
        int locationY = floor(particle->y / blockSize);
        //Get node at that grid location
        ParticleNode* node = grid[locationX][locationY];
        //Create new node for particle we are adding
        ParticleNode* newNode = (ParticleNode*) malloc(sizeof(ParticleNode));
        newNode->particle = particle;
        newNode->next = NULL;
        newNode->prev = NULL;
        newNode->index = i;
        if (node == NULL) { //Add the new particle node
            newNode->gridX = locationX;
            newNode->gridY = locationY;
            grid[locationX][locationY] = newNode;
        } else { //Add the new node to the grid but place it ordered in the linked list based on index to make memory lookup slightly faster
            newNode->gridX = locationX;
            newNode->gridY = locationY;
            ParticleNode* firstNode = node;
            ParticleNode* pastNode = NULL;
            while (node->index < i) {
                pastNode = node;
                node = node->next;
                if (node == NULL) break;
            }
            //Node should equal either null or a node we need to place our new node in front of
            if (node == firstNode) {
                //Just push back the first node
                newNode->next = node;
                node->prev = newNode;
                grid[locationX][locationY] = newNode;
            } else if (node != NULL) {
                //Put the new node between two nodes
                ParticleNode* leftNode = node->prev;
                ParticleNode* rightNode = node;
                newNode->prev = leftNode;
                leftNode->next = newNode;
                newNode->next = rightNode;
                rightNode->prev = newNode;
            } else {
                //Put the new node at the end
                ParticleNode* leftNode = pastNode;
                newNode->prev = leftNode;
                leftNode->next = newNode;
            }
        }
        //Set the new particle node in the particle nodes array
        particleNodes[i] = newNode;
    }
     */
    //Adding code end
    
    
    MPI_Scatterv( particles, partition_sizes, partition_offsets, PARTICLE, local, nlocal, PARTICLE, 0, MPI_COMM_WORLD );
    
    
    
    
    
    
    
    //
    //  simulate a number of time steps
    //
    double simulation_time = read_timer( );
    
    for( int step = 0; step < NSTEPS; step++ )
    {
        navg = 0;
        dmin = 1.0;
        davg = 0.0;
        // 
        //  collect all global data locally (not good idea to do)
        //
        MPI_Allgatherv( local, nlocal, PARTICLE, particles, partition_sizes, partition_offsets, PARTICLE, MPI_COMM_WORLD );
        
        //
        //  save current step if necessary (slightly different semantics than in other codes)
        //
        if( find_option( argc, argv, "-no" ) == -1 )
          if( fsave && (step%SAVEFREQ) == 0 )
            save( fsave, n, particles );
        
        /*
        //
        //  compute all forces
        //
        for( int i = 0; i < nlocal; i++ )
        {
            local[i].ax = local[i].ay = 0;
            for (int j = 0; j < n; j++ ) {
                apply_force( local[i], particles[j], &dmin, &davg, &navg );
            }
        }

        //
        //  move particles
        //
        for( int i = 0; i < nlocal; i++ ) {
            move( local[i] );
        }*/
        // clear bins at each time step
        for (int m = 0; m < numbins; m++)
            bins[m].clear();
        
        // place particles in bins
        for (int i = 0; i < n; i++)
            bins[calculateBinNum(particles[i],binsPerSide)].push_back(particles + i);
        
        //
        //  compute forces here
        //
        for( int p = 0; p < nlocal; p++ )
        {
            local[p].ax = local[p].ay = 0;
            
            // find current particle's bin, handle boundaries
            int cbin = calculateBinNum( local[p], binsPerSide );
            int lowi = -1, highi = 1, lowj = -1, highj = 1;
            if (cbin < binsPerSide) lowj = 0;
            if (cbin % binsPerSide == 0) lowi = 0;
            if (cbin % binsPerSide == (binsPerSide-1)) highi = 0;
            if (cbin >= binsPerSide*(binsPerSide-1)) highj = 0;
            
            // apply nearby forces
            for (int i = lowi; i <= highi; i++)
                for (int j = lowj; j <= highj; j++)
                {
                    int nbin = cbin + i + binsPerSide*j;
                    for (int k = 0; k < bins[nbin].size(); k++ )
                        apply_force( local[p], *bins[nbin][k], &dmin, &davg, &navg);
                }
        }
        
        //
        //  move particles
        //
        for( int p = 0; p < nlocal; p++ )
            move( local[p] );
        
        
        
        if( find_option( argc, argv, "-no" ) == -1 )
        {
            
            MPI_Reduce(&davg,&rdavg,1,MPI_DOUBLE,MPI_SUM,0,MPI_COMM_WORLD);
            MPI_Reduce(&navg,&rnavg,1,MPI_INT,MPI_SUM,0,MPI_COMM_WORLD);
            MPI_Reduce(&dmin,&rdmin,1,MPI_DOUBLE,MPI_MIN,0,MPI_COMM_WORLD);
            
            
            if (rank == 0){
                //
                // Computing statistical data
                //
                if (rnavg) {
                    absavg +=  rdavg/rnavg;
                    nabsavg++;
                }
                if (rdmin < absmin) absmin = rdmin;
            }
        }
    }
    simulation_time = read_timer( ) - simulation_time;
  
    if (rank == 0) {  
      printf( "n = %d, simulation time = %g seconds", n, simulation_time);

      if( find_option( argc, argv, "-no" ) == -1 )
      {
        if (nabsavg) absavg /= nabsavg;
      // 
      //  -the minimum distance absmin between 2 particles during the run of the simulation
      //  -A Correct simulation will have particles stay at greater than 0.4 (of cutoff) with typical values between .7-.8
      //  -A simulation were particles don't interact correctly will be less than 0.4 (of cutoff) with typical values between .01-.05
      //
      //  -The average distance absavg is ~.95 when most particles are interacting correctly and ~.66 when no particles are interacting
      //
      printf( ", absmin = %lf, absavg = %lf", absmin, absavg);
      if (absmin < 0.4) printf ("\nThe minimum distance is below 0.4 meaning that some particle is not interacting");
      if (absavg < 0.8) printf ("\nThe average distance is below 0.8 meaning that most particles are not interacting");
      }
      printf("\n");     
        
      //  
      // Printing summary data
      //  
      if( fsum)
        fprintf(fsum,"%d %d %g\n",n,n_proc,simulation_time);
    }
  
    //
    //  release resources
    //
    if ( fsum )
        fclose( fsum );
    free( partition_offsets );
    free( partition_sizes );
    free( local );
    free( particles );
    delete [] bins;
    /*
    for (int i=0; i<gridSize; i++) {
        free(grid[i]);
    }
    free(grid);
    for (int i = 0; i < n; i++) {
        free(particleNodes[i]);
    }
    free(particleNodes);
    */
    if( fsave )
        fclose( fsave );
    
    MPI_Finalize( );
    
    return 0;
}
