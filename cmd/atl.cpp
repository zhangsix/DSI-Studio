#include <boost/thread.hpp>
#include <QFileInfo>
#include <QApplication>
#include "image/image.hpp"
#include "boost/program_options.hpp"
#include "mapping/fa_template.hpp"
#include "libs/gzip_interface.hpp"
#include "mapping/atlas.hpp"

namespace po = boost::program_options;
extern fa_template fa_template_imp;
extern std::vector<atlas> atlas_list;
std::string get_fa_template_path(void);
int atl(int ac, char *av[])
{
    po::options_description norm_desc("fiber tracking options");
    norm_desc.add_options()
    ("help", "help message")
    ("action", po::value<std::string>(), "atl: output atlas")
    ("source", po::value<std::string>(), "assign the .fib file name")
    ("order", po::value<int>()->default_value(0), "normalization order (0~3)")
    ("thread_count", po::value<int>()->default_value(4), "thread count")
    ("atlas", po::value<std::string>(), "atlas name")
    ;

    if(!ac)
    {
        std::cout << norm_desc << std::endl;
        return 1;
    }

    po::variables_map vm;
    po::store(po::command_line_parser(ac, av).options(norm_desc).run(), vm);
    po::notify(vm);


    gz_mat_read mat_reader;
    std::string file_name = vm["source"].as<std::string>();
    std::cout << "loading " << file_name << "..." <<std::endl;
    if(!QFileInfo(file_name.c_str()).exists())
    {
        std::cout << file_name << " does not exist. terminating..." << std::endl;
        return 0;
    }
    if (!mat_reader.load_from_file(file_name.c_str()))
    {
        std::cout << "Invalid MAT file format" << std::endl;
        return 0;
    }

    unsigned int col,row;
    const unsigned short* dim = 0;
    const float* vs = 0;
    const float* fa0 = 0;
    if(!mat_reader.read("dimension",row,col,dim) ||
       !mat_reader.read("voxel_size",row,col,vs) ||
       !mat_reader.read("fa0",row,col,fa0))
    {
        std::cout << "Invalid file format" << std::endl;
        return 0;
    }


    std::cout << "loading template..." << std::endl;
    if(!fa_template_imp.load_from_file(get_fa_template_path().c_str()))
    {
        std::string error_str = "Cannot find template file at ";
        error_str += get_fa_template_path();
        std::cout << error_str << std::endl;
        return -1;
    }
    std::cout << "loading atlas..." << std::endl;
    {
        std::string atlas_name = vm["atlas"].as<std::string>();
        std::replace(atlas_name.begin(),atlas_name.end(),',',' ');
        std::istringstream in(atlas_name);
        std::vector<std::string> name_list;
        std::copy(std::istream_iterator<std::string>(in),
                  std::istream_iterator<std::string>(),std::back_inserter(name_list));

        for(unsigned int index = 0;index < name_list.size();++index)
        {
            std::string atlas_path = QCoreApplication::applicationDirPath().toLocal8Bit().begin();
            atlas_path += "/atlas/";
            atlas_path += name_list[index];
            atlas_path += ".nii.gz";
            atlas_list.push_back(atlas());
            if(!atlas_list.back().load_from_file(atlas_path.c_str()))
            {
                std::cout << "Cannot load atlas " << atlas_path << std::endl;
                return 0;
            }
            std::cout << name_list[index] << " loaded." << std::endl;
            atlas_list.back().name = name_list[index];

        }
    }


    const float* trans = 0;
    //QSDR
    if(mat_reader.read("trans",row,col,trans))
    {
        std::cout << "Transformation matrix found." << std::endl;
        image::geometry<3> qsdr_geo(dim);
        for(unsigned int i = 0;i < atlas_list.size();++i)
        {
            for(unsigned int j = 0;j < atlas_list[i].get_list().size();++j)
            {
                std::string output = file_name;
                output += ".";
                output += atlas_list[i].name;
                output += ".";
                output += atlas_list[i].get_list()[j];
                output += ".nii.gz";
                image::basic_image<unsigned char,3> roi(qsdr_geo);
                for(image::pixel_index<3> index;qsdr_geo.is_valid(index);index.next(qsdr_geo))
                {
                    image::vector<3,float> pos(index),mni;
                    image::vector_transformation(pos.begin(),mni.begin(),trans,image::vdim<3>());
                    if (atlas_list[i].is_labeled_as(mni, j))
                        roi[index.index()] = 1;
                }
                image::io::nifti out;
                out.set_voxel_size(vs);
                out.set_image_transformation(trans);
                out << roi;
                out.save_to_file(output.c_str());
                std::cout << "save " << output << std::endl;
            }
        }
        return 0;
    }


    std::cout << "perform image registration..." << std::endl;
    image::basic_image<float,3> from(fa0,image::geometry<3>(dim[0],dim[1],dim[2]));
    image::basic_image<float,3>& to = fa_template_imp.I;
    image::affine_transform<3,float> arg;
    arg.scaling[0] = vs[0] / std::fabs(fa_template_imp.tran[0]);
    arg.scaling[1] = vs[1] / std::fabs(fa_template_imp.tran[5]);
    arg.scaling[2] = vs[2] / std::fabs(fa_template_imp.tran[10]);
    image::reg::align_center(from,to,arg);

    image::filter::gaussian(from);
    from -= image::segmentation::otsu_threshold(from);
    image::lower_threshold(from,0.0);

    image::normalize(from,1.0);
    image::normalize(to,1.0);

    bool terminated = false;
    std::cout << "perform linear registration..." << std::endl;
    image::reg::linear(from,to,arg,image::reg::affine,image::reg::mutual_information(),terminated);
    image::transformation_matrix<3,float> T(arg,from.geometry(),to.geometry()),iT(arg,from.geometry(),to.geometry());
    iT.inverse();


    // output linear registration
    float T_buf[16];
    T.save_to_transform(T_buf);
    T_buf[15] = 1.0;
    std::copy(T_buf,T_buf+4,std::ostream_iterator<float>(std::cout," "));
    std::cout << std::endl;
    std::copy(T_buf+4,T_buf+8,std::ostream_iterator<float>(std::cout," "));
    std::cout << std::endl;
    std::copy(T_buf+8,T_buf+12,std::ostream_iterator<float>(std::cout," "));
    std::cout << std::endl;


    image::basic_image<float,3> new_from(to.geometry());
    image::resample(from,new_from,iT);


    std::cout << "perform nonlinear registration..." << std::endl;
    //image::reg::bfnorm(new_from,to,*bnorm_data,*terminated);
    unsigned int factor = vm["order"].as<int>() + 1;
    unsigned int thread_count = vm["thread_count"].as<int>();
    std::cout << "order=" << vm["order"].as<int>() << std::endl;
    std::cout << "thread count=" << thread_count << std::endl;

    image::reg::bfnorm_mapping<float,3> mni(new_from.geometry(),image::geometry<3>(factor*7,factor*9,factor*7));
    image::reg::bfnorm_mrqcof<image::basic_image<float,3>,float> bf_optimize(new_from,to,mni,thread_count);
    for(int iter = 0; iter < 16; ++iter)
    {
        bf_optimize.start();
        boost::thread_group threads;
        for (unsigned int index = 1;index < thread_count;++index)
                threads.add_thread(new boost::thread(
                    &image::reg::bfnorm_mrqcof<image::basic_image<float,3>,float>::run<bool>,&bf_optimize,index,boost::ref(terminated)));
        bf_optimize.run(0,terminated);
        if(thread_count > 1)
            threads.join_all();
        bf_optimize.end();
        boost::thread_group threads2;
        for (unsigned int index = 1;index < thread_count;++index)
                threads2.add_thread(new boost::thread(
                    &image::reg::bfnorm_mrqcof<image::basic_image<float,3>,float>::run2,&bf_optimize,index,40));
        bf_optimize.run2(0,40);
        if(thread_count > 1)
            threads2.join_all();
    }

    image::basic_image<image::vector<3>,3> mapping(from.geometry());
    for(image::pixel_index<3> index;from.geometry().is_valid(index);index.next(from.geometry()))
    {
        image::vector<3,float> pos;
        T(index,pos);// from -> new_from
        mni(pos,mapping[index.index()]); // new_from -> to
        fa_template_imp.to_mni(mapping[index.index()]);
    }

    float out_trans[16];
    image::matrix::product(fa_template_imp.tran.begin(),T_buf,out_trans,image::dyndim(4,4),image::dyndim(4,4));
    for(unsigned int i = 0;i < atlas_list.size();++i)
    {
        for(unsigned int j = 0;j < atlas_list[i].get_list().size();++j)
        {
            std::string output = file_name;
            output += ".";
            output += atlas_list[i].name;
            output += ".";
            output += atlas_list[i].get_list()[j];
            output += ".nii.gz";

            image::basic_image<unsigned char,3> roi(from.geometry());
            for(unsigned int k = 0;k < from.size();++k)
                if (atlas_list[i].is_labeled_as(mapping[k], j))
                    roi[k] = 1;
            image::io::nifti out;
            out.set_voxel_size(vs);
            out.set_image_transformation(out_trans);
            image::flip_xy(roi);
            out << roi;
            out.save_to_file(output.c_str());
            std::cout << "save " << output << std::endl;
        }
    }
}
