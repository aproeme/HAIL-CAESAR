#include <cmath>
#include "libgeodecomp.h"
#include "catchmentmodel/LSDCatchmentModel.hpp"

//
// Using convention that the origin is at NW corner and South is positive in y direction, East positive in x direction
//



using namespace LibGeoDecomp;


#define STEPS 200
#define XDIM 21
#define YDIM 21
#define outputFrequency 1
#define edgeslope 0.005
#define persistent_water_depth 1.0;

// Define possible cell types to implement boundary conditions
// Note: edges include neither corner cells nor corner+1 cells (where the latter are declared)
enum CellType {INTERNAL, \
	       EDGE_WEST, EDGE_NORTH, EDGE_EAST, EDGE_SOUTH, \
	       CORNER_NW, CORNER_NE, CORNER_SE, CORNER_SW, \
	       NODATA};
// should declare CellType enum  elsewhere (header file?). Used in both both Cell and CellInitializer classes. 




class Cell
{
public:
  class API :
    public APITraits::HasStencil<Stencils::VonNeumann<2,1> >
  {};

  Cell(CellType celltype_in = INTERNAL,		\
       double elev_in = 0.0,			\
       double water_depth_in = 0.0,		\
       double qx_in = 0.0,			\
       double qy_in = 0.0,			\
       bool persistent_water_in = false)					\
    : celltype(celltype_in), elev(elev_in), water_depth(water_depth_in), qx(qx_in), qy(qy_in), persistent_water(persistent_water_in)
  {}
  
  CellType celltype;
  double elev, water_depth;
  double qx, qy;
  TNT::Array1D<double> vel_dir = TNT::Array1D<double> (9, 0.0); // refactor (redefinition of declaration in include/catchmentmodel/LSDCatchmentModel.hpp
  static constexpr double waterinput = 0.0; // refactor (already declared in include/catchmentmodel/LSDCatchmentModel.hpp)
  static constexpr double water_depth_erosion_threshold = 0.0; // ?
  unsigned rfnum = 1; // refactor (already declared in include/catchmentmodel/LSDCatchmentModel.hpp)
  
  
  // refactor - Should replace these defines with type alias declarations (= C++11 template typedef)
  // refactor - check that grid orientation makes sense (write test)
#define WEST neighborhood[Coord<2>(-1, 0)]   
#define EAST neighborhood[Coord<2>( 1, 0)]  
#define NORTH neighborhood[Coord<2>( 0, -1)] 
#define SOUTH neighborhood[Coord<2>( 0,  1)] 


  // DEFINING TEMPORARILY TO BE ABLE TO COMPILE - refactor to read from file
  // ***********************************************************************
  // default values taken from Boscastle_test_paramfile_March2018.params and LSDCatchmentModel.cpp in master branch
  double hflow_threshold = 0.00001;
  double DX = 1.0; 
  double DY = 1.0;
  double courant_number = 0.5;
  double maxdepth = 0.01;
  double time_factor = 1.0;
  double local_time_factor;
  double gravity = 9.8;
  double mannings = 0.04;
  double froude_limit = 0.8;
  bool persistent_water;
  // ***********************************************************************
  

  
  
  
  
  
  template<typename COORD_MAP>
  void update(const COORD_MAP& neighborhood, unsigned nanoStep)
  {
    set_global_timefactor();
    set_local_timefactor();
    water_flux_out();
    flow_route_x(neighborhood);
    flow_route_y(neighborhood);
    depth_update(neighborhood);
  } 




  // =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
  // WATER FLUXES OUT OF CATCHMENT
  // =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
  //
  // Calculate the water coming out and zero any water depths at the edges
  // This will actually set it to the minimum water depth
  // This must be done so that water can still move sediment to the edge of the catchment
  // and hence remove it from the catchment. (otherwise you would get sediment build
  // up around the edges.
  //
  // =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
  void water_flux_out()
  {
    // must still figure out best way to accumulate total across cells during update executed by LibGeodecomp
    switch(celltype){
    case INTERNAL:
      break;
    case EDGE_WEST:
    case EDGE_NORTH:
    case EDGE_EAST:
    case EDGE_SOUTH:
    case CORNER_NW:
    case CORNER_NE:
    case CORNER_SE:
    case CORNER_SW:
      if (water_depth > water_depth_erosion_threshold) water_depth = water_depth_erosion_threshold;
      break;
    default:
      std::cout << "\n\n WARNING: no water_flux_out rule specified for cell type " << celltype << "\n\n";
      break;
    }
  }
  


  
  
  
  // =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
  // THE WATER ROUTING ALGORITHM: LISFLOOD-FP
  //
  //           X direction
  //
  // =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
  template<typename COORD_MAP>
  void flow_route_x(const COORD_MAP& neighborhood)
  {
    double hflow;
    double tempslope;
    double west_elev;
    double west_water_depth;
    
    switch (celltype){
    case INTERNAL:
    case EDGE_NORTH:
    case EDGE_SOUTH:
      west_elev = WEST.elev;
      west_water_depth = WEST.water_depth;
      tempslope = ((west_elev + west_water_depth) - (elev + water_depth)) / DX;
      break;
    case EDGE_WEST:
    case CORNER_NW:
    case CORNER_SW:
      west_elev = -9999;
      west_water_depth = 0.0;
      tempslope = edgeslope;
      break;
    case EDGE_EAST:
    case CORNER_NE:
    case CORNER_SE:
      west_elev = WEST.elev;
      west_water_depth = WEST.water_depth;
      tempslope = edgeslope;
      break;
    default:
      std::cout << "\n\n WARNING: no x-direction flow route rule specified for cell type " << celltype << "\n\n";
      break;
    }

    
    if (water_depth > 0 || west_water_depth > 0)
      {
	hflow = std::max(elev + water_depth, west_elev + west_water_depth) - std::max(elev, west_elev);
	if (hflow > hflow_threshold)
	  {
	    update_q(qx, hflow, tempslope);
	    froude_check(qx, hflow);
	    discharge_check(qx, west_water_depth, DX);
	  }
	else
	  {
	    qx = 0.0;
	    // qxs = 0.0;
	  }
      }
    

    
    
    // calc velocity now
    //if (qx > 0)
    //  {
    //	vel_dir[7] = qx / hflow;
    //  }
    // refactor: old code tries to update vel_dir belonging to neighbour sites - can't do this because neighborhood is passed as a CONST. Should make flow_route only modify its own vel_dir components, based on neighbouring qx qy (i.e. flip around the current logic of modifying neigbouring vel_dir based on local qx qy)
    // But need all cells to record their hflow (or precompute and store -qx/hflow)
    /*	if (qx < 0)
	{
	WEST.vel_dir[3] = (0 - qx) / hflow;
	}

	}*/
  }




  // =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
  // THE WATER ROUTING ALGORITHM: LISFLOOD-FP
  //
  //           Y direction
  //
  // =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
  template<typename COORD_MAP>
  void flow_route_y(const COORD_MAP& neighborhood)
  {
    double hflow;
    double tempslope;
    double north_elev;
    double north_water_depth;
    
    switch (celltype){
    case INTERNAL:
    case EDGE_WEST:
    case EDGE_EAST:
      north_elev = NORTH.elev;
      north_water_depth = NORTH.water_depth;
      tempslope = ((north_elev + north_water_depth) - (elev + water_depth)) / DY;
    break;
    case EDGE_NORTH:
    case CORNER_NW:
    case CORNER_NE:
      north_elev = -9999;
      north_water_depth = 0.0;
      tempslope = edgeslope;
      break;
    case EDGE_SOUTH:
    case CORNER_SW:
    case CORNER_SE:
      north_elev = NORTH.elev;
      north_water_depth = NORTH.water_depth;
      tempslope = edgeslope;
      break;
    default:
      std::cout << "\n\n WARNING: no y-direction flow route rule specified for cell type " << static_cast<int>(celltype) << "\n\n";
      break;
    }

  
    if (water_depth > 0 || north_water_depth > 0)
      {
	hflow = std::max(elev + water_depth, north_elev + north_water_depth) - std::max(elev, north_elev);
	if (hflow > hflow_threshold)
	  {
	    update_q(qy, hflow, tempslope);
	    froude_check(qy, hflow);
	    discharge_check(qy, north_water_depth, DY);
	  }
	else
	  {
	    qy = 0.0;
	    // qys = 0.0;
	  }
      }

    

    
    // calc velocity now
    //if (qy > 0)
    //  {
    //vel_dir[1] = qy / hflow;
    // }
    //refactor: old code tries to update vel_dir belonging to neighbour sites - can't do this because neighborhood is passed as a CONST. Should make flow_route only modify its own vel_dir components, based on neighbouring qx qy (i.e. flip around the current logic of modifying neigbouring vel_dir based on local qx qy)
    // But need all cells to record their hflow (or precompute and store -qx/hflow)

    /*		if (qx < 0)
		{
		NORTH.vel_dir[5] = (0 - qy) / hflow;
		}
		
		}*/
  }
  



  

  
  // =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
  // DEPTH UPDATE
  //
  // =-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
  template<typename COORD_MAP>
  void depth_update(const COORD_MAP& neighborhood)
  {
    double east_qx;
    double south_qy;
    
    switch (celltype){
    case INTERNAL:
    case EDGE_NORTH:
    case EDGE_WEST:
    case CORNER_NW:
      east_qx = EAST.qx;
      south_qy = SOUTH.qy;
      update_water_depth(east_qx, south_qy);
      break;
    case EDGE_EAST:
    case CORNER_NE:
      east_qx = 0.0;
      south_qy = SOUTH.qy;
      update_water_depth(east_qx, south_qy);
      break;
    case EDGE_SOUTH:
    case CORNER_SW:
      east_qx = EAST.qx;
      south_qy = 0.0;
      update_water_depth(east_qx, south_qy);
      break;
    case CORNER_SE:
      east_qx = 0.0;
      south_qy = 0.0;
      update_water_depth(east_qx, south_qy);
      break;
    case NODATA:
      water_depth = 0.0;
      break;
    default:
      std::cout << "\n\n WARNING: no depth update rule specified for cell type " << CellType(int(celltype))<< "\n\n";
      break;
    }
    
    
    if(persistent_water) water_depth = persistent_water_depth;
      
  }
  
  

  
  void update_water_depth(double east_qx, double south_qy)
  {
    water_depth += local_time_factor * ( (east_qx - qx)/DX + (south_qy - qy)/DY );
  }

  
  void catchment_waterinputs() // refactor - incomplete (include runoffGrid for complex rainfall)
  {
    // refactor - incomplete
    //waterinput = 0;
    catchment_water_input_and_hydrology();
  }
  
  
  void catchment_water_input_and_hydrology()
  {
    // topmodel_runoff(); (call some number of times depending on timescales)
    // calchydrograph();
    // zero_and_calc_drainage_area();
    // get_catchment_input_points();
  }
  
  
  void update_q(double &q, double hflow, double tempslope)
  {
    q = ((q - (gravity * hflow * local_time_factor * tempslope))	\
	  / (1 + gravity * hflow * local_time_factor * (mannings * mannings) \
	     * std::abs(qy) / std::pow(hflow, (10.0 / 3.0))));
  }


  // FROUDE NUMBER CHECK
  // need to have these lines to stop too much water moving from
  // one cell to another - resulting in negative discharges
  // which causes a large instability to develop
  // - only in steep catchments really
  void froude_check(double &q, double hflow)
  {
    if ((std::abs(q / hflow) / std::sqrt(gravity * hflow)) > froude_limit)
      {
	q = std::copysign(hflow * (std::sqrt(gravity*hflow) * froude_limit), q);
      }
  }
  
  
  // DISCHARGE MAGNITUDE/TIMESTEP CHECKS
  // If the discharge is too high for this timestep, scale back...
  void discharge_check(double &q, double neighbour_water_depth, double Delta)
  {
    double criterion_magnitude = std::abs(q * local_time_factor / Delta);
    
    if (q > 0 && criterion_magnitude > (water_depth / 4))
      {
	q = ((water_depth * Delta) / 5) / local_time_factor;
      }
    else if (q < 0 && criterion_magnitude > (neighbour_water_depth / 4))
      {
	q = -((neighbour_water_depth * Delta) / 5) / local_time_factor;
      }
  }

  
  void set_global_timefactor()
  {
    if (maxdepth <= 0.1)
      {
	maxdepth = 0.1;
      }
    if (time_factor < (courant_number * (DX / std::sqrt(gravity * (maxdepth)))))
      {
	time_factor = (courant_number * (DX / std::sqrt(gravity * (maxdepth))));
      }
    /*    if (input_output_difference > in_out_difference_allowed && time_factor > (courant_number * (DX / std::sqrt(gravity * (maxdepth)))))
      {
	time_factor = courant_number * (DX / std::sqrt(gravity * (maxdepth)));
	}*/
  }
  
  
  void set_local_timefactor()
  {
    local_time_factor = time_factor;
    if (local_time_factor > (courant_number * (DX  / std::sqrt(gravity * (maxdepth)))))
      {
	local_time_factor = courant_number * (DX / std::sqrt(gravity * (maxdepth)));
      }
  }

  

  
  /*  void set_inputoutput_diff()
  {
    input_output_difference = std::abs(waterinput - waterOut);
    }*/


  

  
  
}; // end of Cell class








class CellInitializer : public SimpleInitializer<Cell>
{
public:
  using SimpleInitializer<Cell>::gridDimensions;  // gridDimensions = dimensions of subgrid (in parallel case), not of overall grid
  
  CellInitializer() : SimpleInitializer<Cell>(Coord<2>(XDIM,YDIM), STEPS) // refactor - make dimensions variable. Second argument is the number of steps to run for
  {}
  
  /*   From libgeodecomp:
       "The initializer sets up the initial state of the grid. For this a
       Simulator will invoke Initializer::grid(). Keep in mind that grid()
       might be called multiple times and that for parallel runs each
       Initializer will be responsible just for a sub-cuboid of the whole
       grid." */
  
  
  void grid(GridBase<Cell, 2> *subdomain)
  {
    CellType celltype;
    
    for (int x=0; x<=XDIM-1; x++)
      {      
	for (int y=0; y<=YDIM-1; y++)
	  {
	    if (y == 0)
	      {
		if (x == 0) celltype = CORNER_NW;
		else if (x == XDIM-1) celltype = CORNER_NE;
		else celltype = EDGE_NORTH; 
	      }
	    else if (y == YDIM-1)
	      {
		if (x == 0) celltype = CORNER_SW; 
		else if (x == XDIM-1) celltype = CORNER_SE; 
		else celltype = EDGE_SOUTH; 
	      }	      
	    else  // i.e. for  2 <= y <= YDIM-2
	      {
		if (x == 0) celltype = EDGE_WEST; 
		else if (x == XDIM-1) celltype = EDGE_EAST; 
		else celltype = INTERNAL;
	      }
	    
	    Coord<2> coordinate(x, y);
	    
	    
	    
	    // DEBUG TEST INITIALISATIONS
	    
	    // Uniform zero elevation, uniform zero water depth
	    //subdomain->set(coordinate, Cell(celltype, 0.0, 0.0, 0.0, 0.0, false));
	    
	    // Uniform zero elevation, uniform 0.5 water depth
	    //subdomain->set(coordinate, Cell(celltype, 0.0, 0.5, 0.0, 0.0, false));
	    
	    // Uniform zero elevation, initial water depth 1.0 in NW corner cell and zero elsewhere
	    //if (x==0 && y==0) subdomain->set(coordinate, Cell(celltype, 0.0, 1.0, 0.0, 0.0));
	    //else subdomain->set(coordinate, Cell(celltype, 0.0, 0.0, 0.0, 0.0));
	    
	    // Uniform zero elevation, water depth always at 1.0 in NW corner cell and zero elsewhere
	    /*if (x==0 && y==0) subdomain->set(coordinate, Cell(persistent, 0.0, 1.0, 0.0, 0.0)); 
	    else subdomain->set(coordinate, Cell(celltype, 0.0, 0.0, 0.0, 0.0));*/
	    
	    // Uniform zero elevation, water depth always at 1.0 in NE corner cell and zero elsewhere
	    //if (x==XDIM-1 && y==0) subdomain->set(coordinate, Cell(persistent, 0.0, 1.0, 0.0, 0.0)); 
	    //else subdomain->set(coordinate, Cell(celltype, 0.0, 0.0, 0.0, 0.0));
	    
	    // Uniform zero elevation, water depth always at 1.0 in SW corner cell and zero elsewhere
	    /*	      if (x==0 && y==YDIM-1) subdomain->set(coordinate, Cell(celltype, 0.0, 1.0, 0.0, 0.0, false)); 
	      else subdomain->set(coordinate, Cell(celltype, 0.0, 0.0, 0.0, 0.0, false));*/
	    
	    // Uniform zero elevation, water depth always at 1.0 in SE corner cell and zero elsewhere
	    /*if (x==XDIM-1 && y==YDIM-1)	subdomain->set(coordinate, Cell(celltype, 0.0, 1.0, 0.0, 0.0, false));
	    else subdomain->set(coordinate, Cell(celltype, 0.0, 0.0, 0.0, 0.0, false));*/
	    
	    // Uniform zero elevation, initial water depth 1.0 in central cell and zero elsewhere
	    //if (x == (int)(XDIM/2) && y == (int)(YDIM/2)) subdomain->set(coordinate, Cell(celltype, 0.0, 1.0, 0.0, 0.0, false)); 
	    //else subdomain->set(coordinate, Cell(celltype, 0.0, 0.0, 0.0, 0.0, false));

	    // Uniform zero elevation, initial water depth 0.5 in a blob of central cells and zero elsewhere
	    //if ( (std::abs(x - (int)(XDIM/2)) < 2) && (std::abs(y - (int)(YDIM/2)) < 2) ) subdomain->set(coordinate, Cell(celltype, 0.0, 0.5, 0.0, 0.0, false)); 
	    //else subdomain->set(coordinate, Cell(celltype, 0.0, 0.0, 0.0, 0.0, false));
	    
	    // Uniform zero elevation, water depth always at 1.0 in central cell, initial water depth zero elsewhere
	    //if (x == (int)(XDIM/2) && y == (int)(YDIM/2)) subdomain->set(coordinate, Cell(celltype, 0.0, 1.0, 0.0, 0.0, true));
	    //if (x == 5 && y == 5) subdomain->set(coordinate, Cell(celltype, 0.0, 1.0, 0.0, 0.0, true)); 
	    //else subdomain->set(coordinate, Cell(celltype, 0.0, 0.0, 0.0, 0.0, false));
	    
	    // Uniform zero elevation, water depth always at 1.0 in central cell and cells near all four corners, initial water depth zero elsewhere
	    //if (x == (int)(XDIM/2) && y == (int)(YDIM/2)) subdomain->set(coordinate, Cell(celltype, 0.0, 1.0, 0.0, 0.0, true)); // Central cell
	    //else if ( (x==4 || x==XDIM-5) && (y==4 || y==YDIM-5)) subdomain->set(coordinate, Cell(celltype, 0.0, 1.0, 0.0, 0.0, true)); // cells near four corners }
	    //else subdomain->set(coordinate, Cell(celltype, 0.0, 0.0, 0.0, 0.0, false));

	    // Constant ascending elevation in y direction, water depth always at 1.0 in central cell and cells near all four corners, initial water depth zero elsewhere
	    /*if (x == (int)(XDIM/2) && y == (int)(YDIM/2)) subdomain->set(coordinate, Cell(celltype, (double)y/(double)YDIM, 1.0, 0.0, 0.0, true)); // Central cell
	    else if ( (x==4 || x==XDIM-5) && (y==4 || y==YDIM-5)) subdomain->set(coordinate, Cell(celltype, (double)y/(double)YDIM, 1.0, 0.0, 0.0, true)); // cells near four corners }
	    else subdomain->set(coordinate, Cell(celltype, (double)y/(double)YDIM, 0.0, 0.0, 0.0, false));*/
	    
	    // Constant ascending elevation in x direction, water depth always 1.0 in central cell and initial water depth zero elsewhere
	    //if (x == (int)(XDIM/2) && y == (int)(YDIM/2)) subdomain->set(coordinate, Cell(celltype, (double)x/(double)XDIM, 1.0, 0.0, 0.0, true)); 
	    //else subdomain->set(coordinate, Cell(celltype, (double)x/(double)XDIM, 0.0, 0.0, 0.0, false));
	    
	    // Constant descending elevation in x direction, water depth always nonzero for band at x=1 and initial water depth zero elsewhere
	    // Should set persistent_water_depth to same nonzero value as initial value chosen below
	    //if (x == 1 && y > 3 && y < YDIM-4) subdomain->set(coordinate, Cell(celltype, 1-(double)x/(double)XDIM, 0.2, 0.0, 0.0, true)); 
	    //else subdomain->set(coordinate, Cell(celltype, 1-(double)x/(double)XDIM, 0.0, 0.0, 0.0, false));

	  }
      }
  }
};






void runSimulation()
{
  SerialSimulator<Cell> sim(new CellInitializer());
  
  //DistributedSimulator<Cell> sim(new CellInitializer());
  
  sim.addWriter(new PPMWriter<Cell>(&Cell::elev, 0.0, 1.0, "elevation", STEPS, Coord<2>(20,20)));
  sim.addWriter(new PPMWriter<Cell>(&Cell::water_depth, 0.0, 1.0, "water_depth", outputFrequency, Coord<2>(20,20)));
  //  sim.addWriter(new PPMWriter<Cell>(&Cell::qx, 0.0, 1.0, "qx", outputFrequency, Coord<2>(100,100)));
  //  sim.addWriter(new PPMWriter<Cell>(&Cell::qy, 0.0, 1.0, "qy", outputFrequency, Coord<2>(100,100)));
  
  sim.addWriter(new TracingWriter<Cell>(outputFrequency, STEPS));

  //sim.addWriter(new ParallelWriter<Cell>("water_depth", outputFrequency));
  
  sim.run();
}


