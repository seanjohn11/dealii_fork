// ---------------------------------------------------------------------
//
// Copyright (C) 2010 - 2021 by the deal.II authors
//
// This file is part of the deal.II library.
//
// The deal.II library is free software; you can use it, redistribute
// it, and/or modify it under the terms of the GNU Lesser General
// Public License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
// The full text of the license can be found in the file LICENSE.md at
// the top level directory of deal.II.
//
// ---------------------------------------------------------------------


// check and illustrate the serialization process for ParticleHandler
// when the triangulation is fully distributed.
// In this case, find_active_cell_around_point() currently cannot be used to
// locate the cells around the particles and so we use a more naive insertion
// mechanism.
#include <deal.II/distributed/fully_distributed_tria.h>

#include <deal.II/fe/mapping_q.h>

#include <deal.II/grid/grid_generator.h>
#include <deal.II/grid/grid_tools.h>

#include <deal.II/particles/particle_handler.h>

#include "serialization.h"

template <int dim, int spacedim>
void
test()
{
  // Generate fulllydistributed triangulation from serial triangulation
  Triangulation<dim, spacedim> basetria;
  GridGenerator::hyper_cube(basetria);
  basetria.refine_global(2);

  auto construction_data =
    TriangulationDescription::Utilities::create_description_from_triangulation(
      basetria, MPI_COMM_WORLD);

  parallel::fullydistributed::Triangulation<dim, spacedim> tr(MPI_COMM_WORLD);
  tr.create_triangulation(construction_data);

  MappingQ<dim, spacedim> mapping(1);

  // Create ParticleHandler and insert two particles
  Particles::ParticleHandler<dim, spacedim> particle_handler(tr, mapping);
  std::vector<Point<spacedim>>              position(2);
  std::vector<Point<dim>>                   reference_position(2);

  for (unsigned int i = 0; i < dim; ++i)
    {
      position[0](i) = 0.125;
      position[1](i) = 0.525;
    }

  Particles::Particle<dim, spacedim> particle1(position[0],
                                               reference_position[0],
                                               0);
  Particles::Particle<dim, spacedim> particle2(position[1],
                                               reference_position[1],
                                               1);

  typename Triangulation<dim, spacedim>::active_cell_iterator cell1(&tr, 2, 0);
  typename Triangulation<dim, spacedim>::active_cell_iterator cell2(&tr, 2, 0);
  particle_handler.insert_particle(particle1, cell1);
  particle_handler.insert_particle(particle2, cell2);

  particle_handler.sort_particles_into_subdomains_and_cells();

  for (auto particle = particle_handler.begin();
       particle != particle_handler.end();
       ++particle)
    deallog << "Before serialization particle id " << particle->get_id()
            << " is in cell " << particle->get_surrounding_cell(tr)
            << std::endl;

  particle_handler.prepare_for_serialization();

  // save additional particle data to archive
  std::ostringstream oss;
  {
    boost::archive::text_oarchive oa(oss, boost::archive::no_header);

    oa << particle_handler;
    tr.save("checkpoint");

    // archive and stream closed when
    // destructors are called
  }
  deallog << oss.str() << std::endl;

  // Now remove all information in tr and particle_handler,
  // this is like creating new objects after a restart
  tr.clear();

  particle_handler.clear();
  particle_handler.initialize(tr, mapping);

  // This should not produce any output
  for (auto particle = particle_handler.begin();
       particle != particle_handler.end();
       ++particle)
    deallog << "In between particle id " << particle->get_id() << " is in cell "
            << particle->get_surrounding_cell(tr) << std::endl;

  // verify correctness of the serialization. Note that the boost
  // archive of the ParticleHandler has to be read before the triangulation
  // (otherwise it does not know if something was stored in the user data of the
  // triangulation).
  {
    std::istringstream            iss(oss.str());
    boost::archive::text_iarchive ia(iss, boost::archive::no_header);

    ia >> particle_handler;
    tr.load("checkpoint");
    particle_handler.deserialize();
  }

  deallog << "After deserialization global number of particles is: "
          << particle_handler.n_global_particles() << std::endl;
  for (auto particle = particle_handler.begin();
       particle != particle_handler.end();
       ++particle)
    deallog << "After serialization particle id " << particle->get_id()
            << " is in cell " << particle->get_surrounding_cell(tr)
            << std::endl;

  deallog << "OK" << std::endl << std::endl;
}


int
main(int argc, char *argv[])
{
  Utilities::MPI::MPI_InitFinalize mpi_initialization(argc, argv, 1);

  MPILogInitAll all;

  deallog.push("2d/2d");
  test<2, 2>();
  deallog.pop();
  deallog.push("2d/3d");
  test<2, 3>();
  deallog.pop();
  deallog.push("3d/3d");
  test<3, 3>();
  deallog.pop();
}