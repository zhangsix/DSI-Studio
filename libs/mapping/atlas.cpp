#include "atlas.hpp"
#include <fstream>
#include <sstream>
#include "libs/gzip_interface.hpp"
#include <QCoreApplication>
#include <QDir>

std::vector<atlas> atlas_list;
void load_atlas(void)
{
    QDir dir = QCoreApplication::applicationDirPath()+ "/atlas";
    QStringList atlas_name_list = dir.entryList(QStringList("*.nii"),QDir::Files|QDir::NoSymLinks);
    atlas_name_list << dir.entryList(QStringList("*.nii.gz"),QDir::Files|QDir::NoSymLinks);
    if(atlas_name_list.empty())
    {
        dir = QDir::currentPath()+ "/atlas";
        atlas_name_list = dir.entryList(QStringList("*.nii"),QDir::Files|QDir::NoSymLinks);
        atlas_name_list << dir.entryList(QStringList("*.nii.gz"),QDir::Files|QDir::NoSymLinks);
    }
    if(atlas_name_list.empty())
        return;
    atlas_list.resize(atlas_name_list.size());
    for(int index = 0;index < atlas_name_list.size();++index)
    {
        atlas_list[index].name = QFileInfo(atlas_name_list[index]).baseName().toLocal8Bit().begin();
        atlas_list[index].filename = (dir.absolutePath() + "/" + atlas_name_list[index]).toLocal8Bit().begin();
    }

}

void atlas::load_label(void)
{
    std::string file_name_str(filename);
    std::string text_file_name;
    if (file_name_str.length() > 3 &&
            file_name_str[file_name_str.length()-3] == '.' &&
            file_name_str[file_name_str.length()-2] == 'g' &&
            file_name_str[file_name_str.length()-1] == 'z')
        text_file_name = std::string(file_name_str.begin(),file_name_str.end()-6);
    else
        text_file_name = std::string(file_name_str.begin(),file_name_str.end()-3);
    text_file_name += "txt";
    std::ifstream in(text_file_name.c_str());
    if(!in)
        return;
    std::vector<std::string> text;
    std::string str;
    while(std::getline(in,str))
        text.push_back(str);

    is_bit_labeled = false;

    if(text[0] == "0\t* * * * *")//talairach
    {
        std::map<std::string,std::set<unsigned int> > regions;
        for(int i = 0;i < text.size();++i)
        {
            std::istringstream read_line(text[i]);
            int num;
            read_line >> num;
            std::string region;
            while (read_line >> region)
            {
                if(region == "*")
                    continue;
                regions[region].insert(i);
            }
            index2label.resize(i+1);
        }

        std::map<std::string,std::set<unsigned int> >::iterator iter = regions.begin();
        std::map<std::string,std::set<unsigned int> >::iterator end = regions.end();
        for (int i = 0;iter != end;++iter,++i)
        {
            labels.push_back(iter->first);
            label_num.push_back(label_num.size());// dummy
            label2index.push_back(std::vector<unsigned int>(iter->second.begin(),iter->second.end()));
            for(int j = 0;j < label2index.back().size();++j)
                index2label[label2index[i][j]].push_back(i);
        }
    }
    else
    {
        for(auto& line : text)
        {
            if(line.empty() || line[0] == '#')
                continue;
            std::string txt;
            uint64_t num = 0;
            std::istringstream(line) >> num >> txt;
            if(txt.empty())
                continue;
            label_num.push_back(num);
            labels.push_back(txt);
        }
        if(label_num.size() > 6 &&
           label_num[0] == 1 &&
           label_num[1] == 2 &&
           label_num[2] == 4 &&
           label_num[3] == 8 &&
           label_num[4] == 16 &&
           label_num[5] == 32)
        {
            is_bit_labeled = true;
            for(int i = 6;i < label_num.size();++i)
                label_num[i] = (uint64_t(1) << i);
        }
    }
}

void atlas::load_from_file(void)
{
    gz_nifti nii;
    if(!nii.load_from_file(filename.c_str()))
        throw std::runtime_error("Cannot load atlas file");
    nii >> I;
    transform.identity();
    nii.get_image_transformation(transform.begin());
    transform.inv();

    if(labels.empty())
        load_label();

    if(label2index.empty() && !is_bit_labeled)
    {
        std::vector<unsigned short> hist(1+*std::max_element(I.begin(),I.end()));
        for(int index = 0;index < I.size();++index)
            hist[I[index]] = 1;
        if(labels.empty())
        {
            for(int index = 1;index < hist.size();++index)
                if(hist[index])
                {
                    std::ostringstream out_name;
                    label_num.push_back(index);
                    out_name << "region " << index;
                    labels.push_back(out_name.str());
                }
        }
        else
        {
            //bool modified_atlas = false;
            for(int i = 0;i < labels.size();)
                if(label_num[i] >= hist.size() || !hist[label_num[i]])
                {
                    labels.erase(labels.begin()+i);
                    label_num.erase(label_num.begin()+i);
                    //modified_atlas = true;
                }
            else
                ++i;
            // used to removed empty label
            /*
            if(modified_atlas)
            {
                std::string file_name_str(filename);
                std::string text_file_name;
                if (file_name_str.length() > 3 &&
                        file_name_str[file_name_str.length()-3] == '.' &&
                        file_name_str[file_name_str.length()-2] == 'g' &&
                        file_name_str[file_name_str.length()-1] == 'z')
                    text_file_name = std::string(file_name_str.begin(),file_name_str.end()-6);
                else
                    text_file_name = std::string(file_name_str.begin(),file_name_str.end()-3);
                text_file_name += "txt";
                std::ofstream out(text_file_name.c_str());
                for(int i = 0;i < labels.size();++i)
                    out << label_num[i] << " " << labels[i] << std::endl;
            }*/
        }
    }
}


void mni_to_tal(float& x,float &y, float &z)
{
    x *= 0.9900;
    float ty = 0.9688*y + ((z >= 0) ? 0.0460*z : 0.0420*z) ;
    float tz = -0.0485*y + ((z >= 0) ? 0.9189*z : 0.8390*z) ;
    y = ty;
    z = tz;
}


uint64_t atlas::get_label_at(const image::vector<3,float>& mni_space)
{
    if(I.empty())
        load_from_file();
    image::vector<3,float> atlas_space(mni_space);
    atlas_space.to(transform);
    atlas_space += 0.5;
    atlas_space.floor();
    if(!I.geometry().is_valid(atlas_space))
        return 0;
    return I.at(atlas_space[0],atlas_space[1],atlas_space[2]);
}

std::string atlas::get_label_name_at(const image::vector<3,float>& mni_space)
{
    if(I.empty())
        load_from_file();
    uint64_t l = get_label_at(mni_space);
    if(!l)
        return std::string();
    if(index2label.empty())
    {
        unsigned int pos = std::find(label_num.begin(),label_num.end(),l)-label_num.begin();
        return pos >= labels.size() ? std::string() : labels[pos];
    }
    if(l >= index2label.size())
        return std::string();
    std::string result;
    for(int i = 0;i < index2label[l].size();++i)
    {
        result += labels[index2label[l][i]];
        result += " ";
    }
    return result;
}

bool atlas::is_labeled_as(const image::vector<3,float>& mni_space,unsigned int label_name_index)
{
    if(I.empty())
        load_from_file();
    return label_matched(get_label_at(mni_space),label_name_index);
}
bool atlas::label_matched(uint64_t l,unsigned int label_name_index)
{
    if(I.empty())
        load_from_file();
    if(label_name_index >= label_num.size())
        return false;
    if(is_bit_labeled)
        return l & label_num[label_name_index];
    if(index2label.empty())
        return l == label_num[label_name_index];
    if(l >= index2label.size())
        return false;
    return std::find(index2label[l].begin(),index2label[l].end(),label_name_index) != index2label[l].end();
}

