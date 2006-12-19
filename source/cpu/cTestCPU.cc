/*
 *  cTestCPU.cc
 *  Avida
 *
 *  Called "test_cpu.cc" prior to 11/30/05.
 *  Copyright 2005-2006 Michigan State University. All rights reserved.
 *  Copyright 1999-2003 California Institute of Technology.
 *
 */

#include "cTestCPU.h"

#include "cAvidaContext.h"
#include "cCPUTestInfo.h"
#include "cEnvironment.h"
#include "functions.h"
#include "cGenotype.h"
#include "cHardwareBase.h"
#include "cHardwareManager.h"
#include "cHardwareStatusPrinter.h"
#include "cInstSet.h"
#include "cInstUtil.h"
#include "cOrganism.h"
#include "cPhenotype.h"
#include "cTestCPUInterface.h"
#include "cResourceCount.h"
#include "cResourceLib.h"
#include "cResource.h"
#include "cStringUtil.h"
#include "cWorld.h"
#include "tMatrix.h"

#include <iomanip>

using namespace std;

cTestCPU::cTestCPU(cWorld* world)
{
  m_world = world;
  InitResources();
}  
 
void cTestCPU::InitResources()
{  
   // Setup the resources...
  // unless another 'set' method is called, they default to zero
  m_res_method = RES_STATIC;

  const cResourceLib& resource_lib = m_world->GetEnvironment().GetResourceLib();
  assert(resource_lib.GetSize() >= 0);

  resource_count.SetSize(resource_lib.GetSize());
  d_resources.ResizeClear(resource_lib.GetSize());
  for(int i=0; i<resource_lib.GetSize(); i++) {
    d_resources[i] = 0.0;
  }

  resource_count.ResizeSpatialGrids(1, 1);

  for (int i = 0; i < resource_lib.GetSize(); i++) {
    cResource* res = resource_lib.GetResource(i);
    const double decay = 1.0 - res->GetOutflow();
    resource_count.Setup(i, res->GetName(), res->GetInitial(), 
                           res->GetInflow(), decay,
                           res->GetGeometry(), res->GetXDiffuse(),
                           res->GetXGravity(), res->GetYDiffuse(), 
                           res->GetYGravity(), res->GetInflowX1(), 
                           res->GetInflowX2(), res->GetInflowY1(), 
                           res->GetInflowY2(), res->GetOutflowX1(), 
                           res->GetOutflowX2(), res->GetOutflowY1(), 
                           res->GetOutflowY2(), res->GetCellListPtr(),
                           m_world->GetVerbosity() );
  }

}

void cTestCPU::SetResourcesFromArray(const tArray<double> &resources)
{
  // Set STATIC resources according to the input array
  m_res_method = RES_STATIC;
  
  for(int i = 0; i < d_resources.GetSize(); i++) {
    if(i >= resources.GetSize()) {
      d_resources[i] = 0.0;
    } else {
      d_resources[i] = resources[i];
    }
  }
}


void cTestCPU::SetResourcesFromCell(int cell_x, int cell_y)
{
  // Set DYNAMIC resources according to the input array
  m_res_method = RES_DYNAMIC;

  // Setup the resources...
  const cResourceLib& resource_lib = m_world->GetEnvironment().GetResourceLib();
  assert(resource_lib.GetSize() >= 0);

  // Make a parallel world that consists of one cell
  for (int i = 0; i < resource_lib.GetSize(); i++) {
    cResource* res = resource_lib.GetResource(i);
    const double decay = 1.0 - res->GetOutflow();
    
    // Set up the resource. 
    // (1) Diffusion and gravity don't make sense in a one-cell world.
    // (2) We offset all of the X, Y coords so this cell is at 0,0 (the only valid cell in the world)
    resource_count.Setup(i, res->GetName(), res->GetInitial(), 
                           res->GetInflow(), decay,
                           res->GetGeometry(), 0.0 /*res->GetXDiffuse()*/,
                           0.0 /*res->GetXGravity()*/, 0.0 /*res->GetYDiffuse()*/, 
                           0.0 /*res->GetYGravity()*/, res->GetInflowX1() - cell_x, 
                           res->GetInflowX2() - cell_x, res->GetInflowY1() - cell_y, 
                           res->GetInflowY2() - cell_y, res->GetOutflowX1() - cell_x, 
                           res->GetOutflowX2() - cell_x, res->GetOutflowY1() - cell_y, 
                           res->GetOutflowY2() - cell_y, res->GetCellListPtr(),
                           m_world->GetVerbosity() );
  }
}

void cTestCPU::ModifyResources(const tArray<double>& res_change)
{
  //We only let the testCPU modify the resources if we are using DYNAMIC ones.
  if (m_res_method == RES_DYNAMIC) resource_count.Modify(res_change);
}


// NOTE: This method assumes that the organism is a fresh creation.
bool cTestCPU::ProcessGestation(cAvidaContext& ctx, cCPUTestInfo& test_info, int cur_depth)
{
  assert(test_info.org_array[cur_depth] != NULL);

  cOrganism & organism = *( test_info.org_array[cur_depth] );

  // Determine how long this organism should be tested for...
  int time_allocated = m_world->GetConfig().TEST_CPU_TIME_MOD.Get() * organism.GetGenome().GetSize();

  // Prepare the inputs...
  cur_input = 0;
  cur_receive = 0;

  // Determine if we're tracing and what we need to print.
  cHardwareTracer* tracer = test_info.GetTraceExecution() ? (test_info.GetTracer()) : NULL;

  int time_used = 0;
  organism.GetHardware().SetTrace(tracer);
  while (time_used < time_allocated && organism.GetHardware().GetMemory().GetSize() &&
         organism.GetPhenotype().GetNumDivides() == 0)
  {
    time_used++;
    organism.GetHardware().SingleProcess(ctx);
    // @CAO Need to watch out for parasites.
  }
  organism.GetHardware().SetTrace(NULL);

  // Print out some final info in trace...
  if (tracer != NULL) tracer->TraceTestCPU(time_used, time_allocated, organism.GetHardware().GetMemory().GetSize(),
                                           organism.GetHardware().GetMemory().AsString(), organism.ChildGenome().AsString());

  // For now, always return true.
  return true;
}


bool cTestCPU::TestGenome(cAvidaContext& ctx, cCPUTestInfo& test_info, const cGenome& genome)
{
  test_info.Clear();
  TestGenome_Body(ctx, test_info, genome, 0);

  return test_info.is_viable;
}

bool cTestCPU::TestGenome(cAvidaContext& ctx, cCPUTestInfo& test_info, const cGenome& genome,
                          ofstream& out_fp)
{
  test_info.Clear();
  TestGenome_Body(ctx, test_info, genome, 0);

  ////////////////////////////////////////////////////////////////
  // IsViable() == false
  //   max_depth == 0  : (0) Parent doesn't divide
  //   max_depth == 1  : (2) Parent does divide, but child does not.
  //   max_depth >= 2  : (3) Parent and child do divide, but neither true.
  // ------------------------------------------------------------
  // IsViable() == true
  //   max_depth == 0  : (4) Parent Breed True
  //   max_depth == 1  : (5) Parent NOT Breed True, but Child Does
  //   max_depth >= 2  : (6) Multiple levels of non-breed true.
  ////////////////////////////////////////////////////////////////


  const int depth_comp = Min(test_info.max_depth, 2);
  int repro_type = ((int) test_info.is_viable) * 3 + 1 + depth_comp;
  if (test_info.is_viable == false && depth_comp == 0)  repro_type = 0;

  out_fp << test_info.is_viable << " "
	 << test_info.max_depth << " "
	 << test_info.depth_found << " "
	 << test_info.max_cycle << " "
	 << repro_type << endl;

  return test_info.is_viable;
}

bool cTestCPU::TestGenome_Body(cAvidaContext& ctx, cCPUTestInfo& test_info,
                               const cGenome& genome, int cur_depth)
{
  assert(cur_depth < test_info.generation_tests);

  // Input sizes can vary based on environment settings, must at least initialize
  m_world->GetEnvironment().SetupInputs(ctx, input_array, test_info.GetUseRandomInputs());

  receive_array.Resize(3);
  if (test_info.GetUseRandomInputs()) {
    receive_array[0] = (15 << 24) + ctx.GetRandom().GetUInt(1 << 24);  // 00001111
    receive_array[1] = (51 << 24) + ctx.GetRandom().GetUInt(1 << 24);  // 00110011
    receive_array[2] = (85 << 24) + ctx.GetRandom().GetUInt(1 << 24);  // 01010101
  } else {
    receive_array[0] = 0x0f139f14;  // 00001111 00010011 10011111 00010100
    receive_array[1] = 0x33083ee5;  // 00110011 00001000 00111110 11100101
    receive_array[2] = 0x5562eb41;  // 01010101 01100010 11101011 01000001
  }
  
  if (cur_depth > test_info.max_depth) test_info.max_depth = cur_depth;

  // Setup the organism we're working with now.
  if (test_info.org_array[cur_depth] != NULL) {
    delete test_info.org_array[cur_depth];
  }
  test_info.org_array[cur_depth] = new cOrganism(m_world, ctx, genome);
  cOrganism & organism = *( test_info.org_array[cur_depth] );
  organism.SetOrgInterface(new cTestCPUInterface(this));
  organism.GetPhenotype().SetupInject(genome.GetSize());

  // Run the current organism.
  ProcessGestation(ctx, test_info, cur_depth);

  // Must be able to divide twice in order to form a successful colony,
  // assuming the CPU doesn't get reset on divides.
  //
  // The possibilities after this gestation cycle are:
  //  1: It did not copy at all => Exit this level.
  //  2: It copied true => Check next gestation cycle, or set is_viable.
  //  3: Its copy looks like an ancestor => copy true.
  //  4: It copied false => we must check the child.

  // Case 1:  ////////////////////////////////////
  if (organism.GetPhenotype().GetNumDivides() == 0)  return false;

  // Case 2:  ////////////////////////////////////
  if (organism.GetPhenotype().CopyTrue() == true) {
    test_info.depth_found = cur_depth;
    test_info.is_viable = true;
    return true;
  }

  // Case 3:  ////////////////////////////////////
  bool is_ancestor = false;
  for (int anc_depth = 0; anc_depth < cur_depth; anc_depth++) {
    if (organism.ChildGenome() == test_info.org_array[anc_depth]->GetGenome()){
      is_ancestor = true;
      const int cur_cycle = cur_depth - anc_depth;
      if (test_info.max_cycle < cur_cycle) test_info.max_cycle = cur_cycle;
      test_info.cycle_to = anc_depth;
    }
  }
  if (is_ancestor) {
    test_info.depth_found = cur_depth;
    test_info.is_viable = true;
    return true;
  }

  // Case 4:  ////////////////////////////////////
  // If we haven't reached maximum depth yet, check out the child.
  if (cur_depth+1 < test_info.generation_tests) {
    // Run the child's genome.
    return TestGenome_Body(ctx, test_info, organism.ChildGenome(), cur_depth+1);
  }

  // All options have failed; just return false.
  return false;
}


void cTestCPU::PrintGenome(cAvidaContext& ctx, const cGenome& genome, cString filename,
                           cGenotype* genotype, int update)
{
  if (filename == "") filename.Set("archive/%03d-unnamed.org", genome.GetSize());
    
  cCPUTestInfo test_info;
  TestGenome(ctx, test_info, genome);
  
  // Open the file...
  cDataFile& df = m_world->GetDataFile(filename);
  
  // Print the useful info at the top...
  df.WriteTimeStamp();  
  cString c("");
  
  df.WriteComment(c.Set("Filename........: %s", static_cast<const char*>(filename)));
  
  if (update >= 0) df.WriteComment(c.Set("Update Output...: %d", update));
  else df.WriteComment("Update Output...: N/A");
  
  df.WriteComment(c.Set("Is Viable.......: %d", test_info.IsViable()));
  df.WriteComment(c.Set("Repro Cycle Size: %d", test_info.GetMaxCycle()));
  df.WriteComment(c.Set("Depth to Viable.: %d", test_info.GetDepthFound()));
  
  if (genotype != NULL) {
    df.WriteComment(c.Set("Update Created..: %d", genotype->GetUpdateBorn()));
    df.WriteComment(c.Set("Genotype ID.....: %d", genotype->GetID()));
    df.WriteComment(c.Set("Parent Gen ID...: %d", genotype->GetParentID()));
    df.WriteComment(c.Set("Tree Depth......: %d", genotype->GetDepth()));
    df.WriteComment(c.Set("Parent Distance.: %d", genotype->GetParentDistance()));
  }

  df.WriteComment("");
  
  const int num_levels = test_info.GetMaxDepth() + 1;
  for (int j = 0; j < num_levels; j++) {
    df.WriteComment(c.Set("Generation: %d", j));

    cOrganism* organism = test_info.GetTestOrganism(j);
    assert(organism != NULL);
    cPhenotype& phenotype = organism->GetPhenotype();
    
    df.WriteComment(c.Set("Merit...........: %f", phenotype.GetMerit().GetDouble()));
    df.WriteComment(c.Set("Gestation Time..: %d", phenotype.GetGestationTime()));
    df.WriteComment(c.Set("Fitness.........: %f", phenotype.GetFitness()));
    df.WriteComment(c.Set("Errors..........: %d", phenotype.GetLastNumErrors()));
    df.WriteComment(c.Set("Genome Size.....: %d", organism->GetGenome().GetSize()));
    df.WriteComment(c.Set("Copied Size.....: %d", phenotype.GetCopiedSize()));
    df.WriteComment(c.Set("Executed Size...: %d", phenotype.GetExecutedSize()));
    
    if (phenotype.GetNumDivides() == 0) df.WriteComment("Offspring.......: NONE");
    else if (phenotype.CopyTrue() == true) df.WriteComment("Offspring.......: SELF");
    else if (test_info.GetCycleTo() != -1) df.WriteComment(c.Set("Offspring.......: %d", test_info.GetCycleTo()));
    else df.WriteComment(c.Set("Offspring.......: %d", j + 1));
    
    df.WriteComment("");
  }
  
  df.WriteComment("Tasks Performed:");
  cPhenotype& phenotype = test_info.GetTestPhenotype();
  for (int i = 0; i < m_world->GetEnvironment().GetTaskLib().GetSize(); i++) {
    df.WriteComment(c.Set("%s %d",
                          static_cast<const char*>(m_world->GetEnvironment().GetTaskLib().GetTask(i).GetName()),
                          phenotype.GetLastTaskCount()[i]));
  }

  df.Endl();
  
  // Display the genome
  cInstUtil::SaveGenome(df.GetOFStream(), test_info.GetTestOrganism()->GetHardware().GetInstSet(), genome);
  
  m_world->GetDataFileManager().Remove(filename);
}

