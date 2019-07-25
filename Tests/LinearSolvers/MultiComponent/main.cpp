#include <AMReX.H>
#include <AMReX_ParmParse.H>
#include <AMReX_MultiFab.H>
#include <AMReX_Machine.H>
#include <AMReX_MLMG.H>
#include <AMReX_PlotFileUtil.H>
#include "MCNodalLinOp.H"

using namespace amrex;

int main (int argc, char* argv[])
{
    
    struct {
        int nlevels = 3;
        int nnodes = 32;
    } mesh;
    {
        ParmParse pp("mesh");
        pp.query("nlevels",mesh.nlevels);
        pp.query("nnodes",mesh.nnodes);
    }
    
    struct {
        int verbose = 2;
        int cg_verbose = 0;
        int max_iter = 100;
        int max_fmg_iter = 0 ;
        int linop_maxorder = 2;
        int agglomeration = 1 ;
        int consolidation = 1 ;
    } mlmg;
    {
        ParmParse pp("mlmg");
        pp.query("verbose",mlmg.verbose);
        pp.query("cg_verbose",mlmg.cg_verbose );
        pp.query("max_iter",mlmg.max_iter);
        pp.query("max_fmg_iter",mlmg.max_fmg_iter);
        pp.query("agglomeration",mlmg.agglomeration);
        pp.query("consolidation",mlmg.consolidation);
    }
    
    //struct {
    //    //Vector coeff;
    //} operator;
    //{
    //    ParmParse pp("operator");
    //    //pp.queryarr("operator",operator.coeff);
    //}
    
    int nlevels = 3, nnodes = 32;


    Initialize(argc, argv);
    
    Vector<Geometry> geom;
  	Vector<BoxArray> cgrids, ngrids;
 	Vector<DistributionMapping> dmap;
  	Vector<MultiFab> solution, rhs;

 	geom.resize(mesh.nlevels);
 	cgrids.resize(mesh.nlevels);
 	ngrids.resize(mesh.nlevels);
 	dmap.resize(mesh.nlevels);

 	solution.resize(mesh.nlevels);
 	rhs.resize(mesh.nlevels);

	RealBox rb({AMREX_D_DECL(0.0,0.0,0.0)},
			          {AMREX_D_DECL(1.0,1.0,1.0)});
	Geometry::Setup(&rb, 0);

	Box NDomain(IntVect{AMREX_D_DECL(0,0,0)}, 
                IntVect{AMREX_D_DECL(mesh.nnodes,mesh.nnodes,mesh.nnodes)}, 
                IntVect::TheNodeVector());
	Box CDomain = convert(NDomain, IntVect::TheCellVector());

	Box domain = CDomain;
 	for (int ilev = 0; ilev < mesh.nlevels; ++ilev)
 		{
 			geom[ilev].define(domain);
 			domain.refine(2);
 		}
	Box cdomain = CDomain;

 	for (int ilev = 0; ilev < mesh.nlevels; ++ilev)
	{
		cgrids[ilev].define(cdomain);
		cgrids[ilev].maxSize(10000); // TODO
		cdomain.grow(-mesh.nnodes/4); 
		cdomain.refine(2); 
		ngrids[ilev] = cgrids[ilev];
		ngrids[ilev].convert(IntVect::TheNodeVector());
	}

 	int ncomp = 2;
    int nghost = 2;
 	for (int ilev = 0; ilev < mesh.nlevels; ++ilev)
 	{
 		dmap   [ilev].define(cgrids[ilev]);
 		solution[ilev].define(ngrids[ilev], dmap[ilev], ncomp, nghost); 
        solution[ilev].setVal(0.0);
 		rhs     [ilev].define(ngrids[ilev], dmap[ilev], ncomp, nghost);
        rhs     [ilev].setVal(0.0);
           
	    Box domain(geom[ilev].Domain());
        const Real* DX = geom[ilev].CellSize();
	    domain.convert(IntVect::TheNodeVector());
	    domain.grow(-1); // Shrink domain so we don't operate on any boundaries            
        for (MFIter mfi(solution[ilev], TilingIfNotGPU()); mfi.isValid(); ++mfi)
    	{
    		Box bx = mfi.tilebox();
    		bx.grow(1);        // Expand to cover first layer of ghost nodes
    		bx = bx & domain;  // Take intersection of box and the problem domain
		
	   		Array4<Real> const& SOL  = solution[ilev].array(mfi);
    		Array4<Real> const& RHS  = rhs[ilev].array(mfi);
    		for (int n = 0; n < ncomp; n++)
    			ParallelFor (bx,[=] AMREX_GPU_DEVICE(int i, int j, int k) {
                    
                    Real x1 = i*DX[0], x2 = j*DX[1], x3 = k*DX[2];

                    //if (n==0) 
                    RHS(i,j,k,n) = x1*(1.0 - x1) * x2 * (1.0 - x2) * x3 * (1.0 - x3);
                    //else RHS(i,j,k,n) = 0.0;
    			});         
 	    }
    }
         

    LPInfo info;
    //info.setAgglomeration(mlmg.agglomeration);
    //info.setConsolidation(mlmg.consolidation);
    MCNodalLinOp linop;
    linop.define(geom,cgrids,dmap,info);
    linop.setCoeff({{{1.0, 0.0},
                     {0.0, 1.0}}});

    MLMG solver(linop);
    solver.setCFStrategy(MLMG::CFStrategy::ghostnodes);
    solver.setVerbose(mlmg.verbose);
    solver.setCGVerbose(mlmg.cg_verbose);
    solver.setMaxIter(mlmg.max_iter);
    solver.setMaxFmgIter(mlmg.max_fmg_iter);
    
    

    Real tol_rel = 1E-8, tol_abs = 1E-8;
    solver.solve(GetVecOfPtrs(solution),GetVecOfConstPtrs(rhs),tol_rel,tol_abs);

    //solver.apply(GetVecOfPtrs(solution),GetVecOfPtrs(rhs));

    WriteMLMF ("solution",GetVecOfConstPtrs(solution),geom);
    WriteMLMF ("rhs",GetVecOfConstPtrs(rhs),geom);
    
}

