#include <cstdlib>     /* srand, rand */
#include <ctime>
#include "vbc_database.h"
#include "fib_data.hpp"
#include "libs/tracking/tract_model.hpp"
#include "libs/tracking/tracking_thread.hpp"



vbc_database::vbc_database():handle(0),roi_type(0),normalize_qa(true)
{
}

bool vbc_database::create_database(const char* template_name)
{
    handle.reset(new fib_data);
    if(!handle->load_from_file(template_name))
    {
        error_msg = handle->error_msg;
        return false;
    }
    fiber_threshold = 0.6*image::segmentation::otsu_threshold(image::make_image(handle->dir.fa[0],handle->dim));
    handle->db.calculate_si2vi();
    return true;
}
bool vbc_database::load_database(const char* database_name)
{
    handle.reset(new fib_data);
    if(!handle->load_from_file(database_name))
    {
        error_msg = "Invalid fib file:";
        error_msg += handle->error_msg;
        return false;
    }
    fiber_threshold = 0.6*image::segmentation::otsu_threshold(image::make_image(handle->dir.fa[0],handle->dim));
    return handle->db.has_db();
}


int vbc_database::run_track(const tracking& fib,std::vector<std::vector<float> >& tracks,float seed_ratio, unsigned int thread_count)
{
    std::vector<image::vector<3,short> > seed;
    for(image::pixel_index<3> index(handle->dim);index < handle->dim.size();++index)
        if(fib.fa[0][index.index()] > fib.threshold)
            seed.push_back(image::vector<3,short>(index.x(),index.y(),index.z()));
    unsigned int count = seed.size()*seed_ratio/1000.0;
    if(!count)
    {
        tracks.clear();
        return 0;
    }
    ThreadData tracking_thread(false);
    tracking_thread.param.step_size = 1.0; // fixed 1 mm
    tracking_thread.param.smooth_fraction = 0;
    tracking_thread.param.min_points_count3 = 6;
    tracking_thread.param.max_points_count3 = std::max<unsigned int>(6,3.0*500/tracking_thread.param.step_size);
    tracking_thread.tracking_method = 0;// streamline fiber tracking
    tracking_thread.initial_direction = 0;// main directions
    tracking_thread.interpolation_strategy = 0; // trilinear interpolation
    tracking_thread.stop_by_tract = 0;// stop by seed
    tracking_thread.center_seed = 0;// subvoxel seeding
    // if no seed assigned, assign whole brain
    if(roi_list.empty() || std::find(roi_type.begin(),roi_type.end(),3) == roi_type.end())
        tracking_thread.setRegions(fib.dim,seed,3,"whole brain");
    if(!roi_list.empty())
    {
        for(unsigned int index = 0;index < roi_list.size();++index)
            tracking_thread.setRegions(fib.dim,roi_list[index],roi_type[index],"user assigned region");
    }
    tracking_thread.run(fib,thread_count,count,true);
    tracking_thread.track_buffer.swap(tracks);

    if(track_trimming)
    {
        TractModel t(handle);
        t.add_tracts(tracks);
        for(int i = 0;i < track_trimming && t.get_visible_track_count();++i)
            t.trim();
        tracks.swap(t.get_tracts());
    }
    return tracks.size();
}

void cal_hist(const std::vector<std::vector<float> >& track,std::vector<unsigned int>& dist)
{
    for(unsigned int j = 0; j < track.size();++j)
    {
        if(track[j].size() <= 3)
            continue;
        unsigned int length = track[j].size()/3-1;
        if(length < dist.size())
            ++dist[length];
        else
            if(!dist.empty())
                ++dist.back();
    }
}



bool vbc_database::read_subject_data(const std::vector<std::string>& files,std::vector<std::vector<float> >& data)
{
    begin_prog("reading",true);
    data.resize(files.size());
    for(unsigned int index = 0;check_prog(index,files.size());++index)
        if(!handle->db.get_odf_profile(files[index].c_str(),data[index]))
        {
            error_msg = "Cannot read file ";
            error_msg += files[index];
            check_prog(0,0);
            return false;
        }
    begin_prog("reading",false);
    check_prog(0,0);
    return true;
}

void vbc_database::run_permutation_multithread(unsigned int id,unsigned int thread_count,unsigned int permutation_count)
{
    connectometry_result data;
    tracking fib;
    fib.read(*handle);
    fib.threshold = tracking_threshold;
    fib.cull_cos_angle = std::cos(60 * 3.1415926 / 180.0);
    float voxel_density = seeding_density*fib.vs[0]*fib.vs[1]*fib.vs[2];
    std::vector<std::vector<float> > tracks;

    if(model->type == 2) // individual
    {


        bool null = true;
        for(int i = id;i < permutation_count && !terminated;)
        {

            for(unsigned int subject_id = 0;subject_id < individual_data.size() && !terminated;++subject_id)
            {
                stat_model info;
                info.resample(*model.get(),null,true);
                if(null)
                {
                    unsigned int random_subject_id = model->rand_gen(model->subject_index.size());
                    info.individual_data = handle->db.subject_qa[random_subject_id];
                    info.individual_data_sd = normalize_qa ? handle->db.subject_qa_sd[random_subject_id]:1.0;
                }
                else
                {
                    info.individual_data = &(individual_data[subject_id][0]);
                    info.individual_data_sd = normalize_qa ? individual_data_sd[subject_id]:1.0;
                }
                calculate_spm(data,info,normalize_qa);
                fib.fa = data.lesser_ptr;
                run_track(fib,tracks,voxel_density);
                cal_hist(tracks,(null) ? subject_lesser_null : subject_lesser);

                if(output_resampling && !null)
                {
                    std::lock_guard<std::mutex> lock(lock_lesser_tracks);
                    lesser_tracks[subject_id]->add_tracts(tracks,length_threshold);
                    tracks.clear();
                }


                fib.fa = data.greater_ptr;
                run_track(fib,tracks,voxel_density);
                cal_hist(tracks,(null) ? subject_greater_null : subject_greater);

                if(output_resampling && !null)
                {
                    std::lock_guard<std::mutex> lock(lock_greater_tracks);
                    greater_tracks[subject_id]->add_tracts(tracks,length_threshold);
                    tracks.clear();
                }

            }
            if(!null)
            {
                i += thread_count;
                if(id == 0)
                    progress = i*100/permutation_count;
            }
            null = !null;
        }
        if(id == 0)
        for(unsigned int subject_id = 0;subject_id < individual_data.size() && !terminated;++subject_id)
        {
            stat_model info;
            info.resample(*model.get(),false,false);
            info.individual_data = &(individual_data[subject_id][0]);
            info.individual_data_sd = normalize_qa ? individual_data_sd[subject_id]:1.0;
            calculate_spm(*spm_maps[subject_id],info,normalize_qa);
            if(terminated)
                return;
            if(!output_resampling)
            {
                fib.fa = spm_maps[subject_id]->lesser_ptr;
                run_track(fib,tracks,voxel_density*permutation_count,threads.size());
                lesser_tracks[subject_id]->add_tracts(tracks,length_threshold);
                fib.fa = spm_maps[subject_id]->greater_ptr;
                run_track(fib,tracks,voxel_density*permutation_count,threads.size());
                greater_tracks[subject_id]->add_tracts(tracks,length_threshold);
            }
        }
    }
    else
    {

        bool null = true;
        for(int i = id;i < permutation_count && !terminated;)
        {

            stat_model info;
            info.resample(*model.get(),null,true);
            calculate_spm(data,info,normalize_qa);

            fib.fa = data.lesser_ptr;
            unsigned int s = run_track(fib,tracks,voxel_density);
            if(null)
                seed_lesser_null[i] = s;
            else
                seed_lesser[i] = s;

            cal_hist(tracks,(null) ? subject_lesser_null : subject_lesser);

            if(output_resampling && !null)
            {
                std::lock_guard<std::mutex> lock(lock_lesser_tracks);
                lesser_tracks[0]->add_tracts(tracks,length_threshold);
                tracks.clear();
            }

            info.resample(*model.get(),null,true);
            calculate_spm(data,info,normalize_qa);
            fib.fa = data.greater_ptr;
            s = run_track(fib,tracks,voxel_density);
            if(null)
                seed_greater_null[i] = s;
            else
                seed_greater[i] = s;
            cal_hist(tracks,(null) ? subject_greater_null : subject_greater);

            if(output_resampling && !null)
            {
                std::lock_guard<std::mutex> lock(lock_greater_tracks);
                greater_tracks[0]->add_tracts(tracks,length_threshold);
                tracks.clear();
            }

            if(!null)
            {
                i += thread_count;
                if(id == 0)
                    progress = i*100/permutation_count;
            }
            null = !null;
        }
        if(id == 0)
        {
            stat_model info;
            info.resample(*model.get(),false,false);
            calculate_spm(*spm_maps[0],info,normalize_qa);

            if(terminated)
                return;
            if(!output_resampling)
            {
                fib.fa = spm_maps[0]->lesser_ptr;
                run_track(fib,tracks,voxel_density*permutation_count,threads.size());
                lesser_tracks[0]->add_tracts(tracks,length_threshold);
                fib.fa = spm_maps[0]->greater_ptr;
                run_track(fib,tracks,voxel_density*permutation_count,threads.size());
                greater_tracks[0]->add_tracts(tracks,length_threshold);
            }
        }
    }
    if(id == 0 && !terminated)
        progress = 100;
}
void vbc_database::clear(void)
{
    if(!threads.empty())
    {
        terminated = true;
        wait();
        threads.clear();
        terminated = false;
    }
}
void vbc_database::wait(void)
{
    for(int i = 0;i < threads.size();++i)
        threads[i]->wait();
}

void vbc_database::save_tracks_files(std::vector<std::string>& saved_file_name)
{
    for(int i = 0;i < threads.size();++i)
        threads[i]->wait();
    if(trk_file_names.size() != greater_tracks.size())
        throw std::runtime_error("Please assign file name for saving trk files.");
    saved_file_name.clear();
    has_greater_result = false;
    has_lesser_result = false;
    for(unsigned int index = 0;index < greater_tracks.size();++index)
    {
        if(greater_tracks[index]->get_visible_track_count())
        {
            TractModel tracks(handle);
            tracks = *greater_tracks[index];
            while(output_resampling && tracks.get_visible_track_count() && tracks.trim())
                ;
            if(tracks.get_visible_track_count())
                *greater_tracks[index] = tracks;
            greater_tracks[index]->delete_repeated();
            std::ostringstream out1;
            out1 << trk_file_names[index] << ".greater.trk.gz";
            greater_tracks[index]->save_tracts_to_file(out1.str().c_str());
            saved_file_name.push_back(out1.str().c_str());
            greater_tracks_result = "";
            greater_tracks[index]->recognize_report(greater_tracks_result);
            has_greater_result = true;
        }
        else
        {
            std::ostringstream out1;
            out1 << trk_file_names[index] << ".greater.no_trk.txt";
            std::ofstream(out1.str().c_str());
        }
        {
            std::ostringstream out1;
            out1 << trk_file_names[index] << ".greater.fib.gz";
            gz_mat_write mat_write(out1.str().c_str());
            for(unsigned int i = 0;i < handle->mat_reader.size();++i)
            {
                std::string name = handle->mat_reader.name(i);
                if(name == "dimension" || name == "voxel_size" ||
                        name == "odf_vertices" || name == "odf_faces" || name == "trans")
                    mat_write.write(handle->mat_reader[i]);
                if(name == "fa0")
                    mat_write.write("qa_map",handle->dir.fa[0],1,handle->dim.size());
            }
            for(unsigned int i = 0;i < spm_maps[index]->greater_ptr.size();++i)
            {
                std::ostringstream out1,out2;
                out1 << "fa" << i;
                out2 << "index" << i;
                mat_write.write(out1.str().c_str(),spm_maps[index]->greater_ptr[i],1,handle->dim.size());
                mat_write.write(out2.str().c_str(),handle->dir.findex[i],1,handle->dim.size());
            }
        }

        if(lesser_tracks[index]->get_visible_track_count())
        {
            TractModel tracks(handle);
            tracks = *lesser_tracks[index];
            while(output_resampling && tracks.get_visible_track_count() && tracks.trim())
                ;
            if(tracks.get_visible_track_count())
                *lesser_tracks[index] = tracks;
            lesser_tracks[index]->delete_repeated();
            std::ostringstream out1;
            out1 << trk_file_names[index] << ".lesser.trk.gz";
            lesser_tracks[index]->save_tracts_to_file(out1.str().c_str());
            saved_file_name.push_back(out1.str().c_str());
            lesser_tracks_result = "";
            lesser_tracks[index]->recognize_report(lesser_tracks_result);
            has_lesser_result = true;
        }
        else
        {
            std::ostringstream out1;
            out1 << trk_file_names[index] << ".lesser.no_trk.txt";
            std::ofstream(out1.str().c_str());
        }

        {
            std::ostringstream out1;
            out1 << trk_file_names[index] << ".lesser.fib.gz";
            gz_mat_write mat_write(out1.str().c_str());
            for(unsigned int i = 0;i < handle->mat_reader.size();++i)
            {
                std::string name = handle->mat_reader.name(i);
                if(name == "dimension" || name == "voxel_size" ||
                        name == "odf_vertices" || name == "odf_faces" || name == "trans")
                    mat_write.write(handle->mat_reader[i]);
                if(name == "fa0")
                    mat_write.write("qa_map",handle->dir.fa[0],1,handle->dim.size());
            }
            for(unsigned int i = 0;i < spm_maps[index]->greater_ptr.size();++i)
            {
                std::ostringstream out1,out2;
                out1 << "fa" << i;
                out2 << "index" << i;
                mat_write.write(out1.str().c_str(),spm_maps[index]->lesser_ptr[i],1,handle->dim.size());
                mat_write.write(out2.str().c_str(),handle->dir.findex[i],1,handle->dim.size());
            }
        }

    }
}

void vbc_database::run_permutation(unsigned int thread_count,unsigned int permutation_count)
{
    clear();
    terminated = false;
    subject_greater_null.clear();
    subject_greater_null.resize(200);
    subject_lesser_null.clear();
    subject_lesser_null.resize(200);
    subject_greater.clear();
    subject_greater.resize(200);
    subject_lesser.clear();
    subject_lesser.resize(200);
    fdr_greater.clear();
    fdr_greater.resize(200);
    fdr_lesser.clear();
    fdr_lesser.resize(200);

    seed_greater_null.clear();
    seed_greater_null.resize(permutation_count);
    seed_lesser_null.clear();
    seed_lesser_null.resize(permutation_count);
    seed_greater.clear();
    seed_greater.resize(permutation_count);
    seed_lesser.clear();
    seed_lesser.resize(permutation_count);

    model->rand_gen.reset();
    std::srand(0);
    greater_tracks.clear();
    lesser_tracks.clear();
    spm_maps.clear();
    has_greater_result = true;
    has_lesser_result = true;
    greater_tracks_result = "tracks";
    lesser_tracks_result = "tracks";
    unsigned int num_subjects = (model->type == 2 ? individual_data.size():1);
    for(unsigned int index = 0;index < num_subjects;++index)
    {
        greater_tracks.push_back(std::make_shared<TractModel>(handle));
        lesser_tracks.push_back(std::make_shared<TractModel>(handle));
        spm_maps.push_back(std::make_shared<connectometry_result>());
    }
    clear();
    progress = 0;
    for(unsigned int index = 0;index < thread_count;++index)
        threads.push_back(std::make_shared<std::future<void> >(std::async(std::launch::async,
            [this,index,thread_count,permutation_count](){run_permutation_multithread(index,thread_count,permutation_count);})));
}
void vbc_database::calculate_FDR(void)
{
    double sum_greater_null = 0;
    double sum_lesser_null = 0;
    double sum_greater = 0;
    double sum_lesser = 0;
    for(int index = subject_greater_null.size()-1;index >= 0;--index)
    {
        sum_greater_null += subject_greater_null[index];
        sum_lesser_null += subject_lesser_null[index];
        sum_greater += subject_greater[index];
        sum_lesser += subject_lesser[index];
        fdr_greater[index] = (sum_greater > 0.0 && sum_greater_null > 0.0) ? std::min(1.0,sum_greater_null/sum_greater) : 1.0;
        fdr_lesser[index] = (sum_lesser > 0.0 && sum_lesser_null > 0.0) ? std::min(1.0,sum_lesser_null/sum_lesser): 1.0;

    }

}
