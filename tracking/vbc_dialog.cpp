#include <QMessageBox>
#include <QFileDialog>
#include <QInputDialog>
#include <QStringListModel>
#include <QThread>
#include <QGraphicsTextItem>
#include <QGraphicsPixmapItem>
#include "vbc_dialog.hpp"
#include "ui_vbc_dialog.h"
#include "ui_tracking_window.h"
#include "tracking_window.h"
#include "tract/tracttablewidget.h"
#include "libs/tracking/fib_data.hpp"
#include "tracking/atlasdialog.h"
extern std::vector<atlas> atlas_list;

bool load_cerebrum_mask(image::basic_image<char,3>& fp_mask)
{

    QString wm_path;
    wm_path = QCoreApplication::applicationDirPath() + "/mni_icbm152_wm_tal_nlin_asym_09a.nii.gz";
    if(!QFileInfo(wm_path).exists())
        wm_path = QDir::currentPath() + "/mni_icbm152_wm_tal_nlin_asym_09a.nii.gz";
    if(!QFileInfo(wm_path).exists())
        return false;

    gz_nifti read_wm;
    image::basic_image<float,3> wm;
    if(read_wm.load_from_file(wm_path.toStdString().c_str()))
        read_wm.toLPS(wm);
    image::basic_image<char,3> wm_mask(wm.geometry());
    for(unsigned int index = 0;index < wm_mask.size();++index)
        if(wm[index] > 0)
            wm_mask[index] = 1;

    image::matrix<4,4,float> trans,trans1,trans2;
    trans.identity();
    trans1.identity();
    trans2.identity();
    read_wm.get_image_transformation(trans.begin());
    int z = (-trans[11]-23)/trans[10]; // cut off at mni z = -22
    int y1 = (-trans[7]-10)/trans[5];
    int y2 = (-trans[7]-37)/trans[5];
    if(y2 < y1)
        std::swap(y1,y2);
    int x1 = (-trans[3]-16)/trans[0];
    int x2 = (-trans[3]+16)/trans[0];
    if(x2 < x1)
        std::swap(x1,x2);
    auto I1 = make_image(&wm_mask[0]+z*wm.geometry().plane_size(),image::geometry<2>(wm_mask.width(),wm_mask.height()));
    image::fill_rect(I1,image::vector<2,int>(x1,y1),image::vector<2,int>(x2,y2),0);
    auto I2 = make_image(&wm_mask[0]+(z+1)*wm.geometry().plane_size(),image::geometry<2>(wm_mask.width(),wm_mask.height()));
    image::fill_rect(I2,image::vector<2,int>(x1,y1),image::vector<2,int>(x2,y2),0);
    image::morphology::defragment(wm_mask);
    float trans1_[16] = {-1, 0, 0, 78,
                          0,-1, 0, 76,
                          0, 0, 1,-50,
                          0, 0, 0,  1};
    float trans2_[16] = {-2, 0, 0, 78,
                          0,-2, 0, 76,
                          0, 0, 2,-50,
                          0, 0, 0,  1};
    trans.inv();
    trans1 = trans*trans1_;
    trans2 = trans*trans2_;
    if(fp_mask.geometry() == image::geometry<3>(157,189,136)) // 1mm
    {
        image::resample(wm_mask,fp_mask,trans1,image::linear);
        return true;
    }
    if(fp_mask.geometry() == image::geometry<3>(79,95,69)) // 2mm
    {
        image::resample(wm_mask,fp_mask,trans2,image::linear);
        return true;
    }
    return false;
}
QWidget *ROIViewDelegate::createEditor(QWidget *parent,
                                     const QStyleOptionViewItem &option,
                                     const QModelIndex &index) const
{
    if (index.column() == 2)
    {
        QComboBox *comboBox = new QComboBox(parent);
        comboBox->addItem("ROI");
        comboBox->addItem("ROA");
        comboBox->addItem("End");
        comboBox->addItem("Seed");
        comboBox->addItem("Terminative");
        connect(comboBox, SIGNAL(activated(int)), this, SLOT(emitCommitData()));
        return comboBox;
    }
    else
        return QItemDelegate::createEditor(parent,option,index);

}

void ROIViewDelegate::setEditorData(QWidget *editor,
                                  const QModelIndex &index) const
{

    if (index.column() == 2)
        ((QComboBox*)editor)->setCurrentIndex(index.model()->data(index).toString().toInt());
    else
        return QItemDelegate::setEditorData(editor,index);
}

void ROIViewDelegate::setModelData(QWidget *editor, QAbstractItemModel *model,
                                 const QModelIndex &index) const
{
    if (index.column() == 2)
        model->setData(index,QString::number(((QComboBox*)editor)->currentIndex()));
    else
        QItemDelegate::setModelData(editor,model,index);
}

void ROIViewDelegate::emitCommitData()
{
    emit commitData(qobject_cast<QWidget *>(sender()));
}


vbc_dialog::vbc_dialog(QWidget *parent,vbc_database* vbc_ptr,QString db_file_name_,bool gui_) :
    QDialog(parent),vbc(vbc_ptr),db_file_name(db_file_name_),work_dir(QFileInfo(db_file_name_).absoluteDir().absolutePath()),gui(gui_),color_bar(10,256),
    ui(new Ui::vbc_dialog)
{
    color_map.spectrum();
    color_bar.spectrum();
    ui->setupUi(this);
    setMouseTracking(true);
    ui->roi_table->setItemDelegate(new ROIViewDelegate(ui->roi_table));
    ui->roi_table->setAlternatingRowColors(true);
    ui->vbc_view->setScene(&vbc_scene);
    ui->fp_dif_view->setScene(&fp_dif_scene);
    ui->fp_view->setScene(&fp_scene);

    ui->multithread->setValue(QThread::idealThreadCount());
    ui->individual_list->setModel(new QStringListModel);
    ui->individual_list->setSelectionModel(new QItemSelectionModel(ui->individual_list->model()));
    ui->advanced_options->hide();
    ui->subject_list->setColumnCount(3);
    ui->subject_list->setColumnWidth(0,300);
    ui->subject_list->setColumnWidth(1,50);
    ui->subject_list->setColumnWidth(2,50);
    ui->subject_list->setHorizontalHeaderLabels(
                QStringList() << "name" << "value" << "R2");

    ui->dist_table->setColumnCount(7);
    ui->dist_table->setColumnWidth(0,100);
    ui->dist_table->setColumnWidth(1,100);
    ui->dist_table->setColumnWidth(2,100);
    ui->dist_table->setColumnWidth(3,100);
    ui->dist_table->setColumnWidth(4,100);
    ui->dist_table->setColumnWidth(5,100);
    ui->dist_table->setColumnWidth(6,100);

    ui->dist_table->setHorizontalHeaderLabels(
                QStringList() << "length (mm)" << "FDR greater" << "FDR lesser"
                                               << "null greater pdf" << "null lesser pdf"
                                               << "greater pdf" << "lesser pdf");


    ui->subject_view->setCurrentIndex(0);
    ui->fp_splitter->setSizes(QList<int>() << 100 << 300);

    update_subject_list();


    on_view_x_toggled(true);

    ui->x_pos->setMaximum(vbc->handle->dim[0]-1);
    ui->y_pos->setMaximum(vbc->handle->dim[1]-1);
    ui->z_pos->setMaximum(vbc->handle->dim[2]-1);

    // dist report
    connect(ui->span_to,SIGNAL(valueChanged(int)),this,SLOT(show_report()));
    connect(ui->span_to,SIGNAL(valueChanged(int)),this,SLOT(show_fdr_report()));
    connect(ui->view_legend,SIGNAL(toggled(bool)),this,SLOT(show_report()));
    connect(ui->show_null_greater,SIGNAL(toggled(bool)),this,SLOT(show_report()));
    connect(ui->show_null_lesser,SIGNAL(toggled(bool)),this,SLOT(show_report()));
    connect(ui->show_greater,SIGNAL(toggled(bool)),this,SLOT(show_report()));
    connect(ui->show_lesser,SIGNAL(toggled(bool)),this,SLOT(show_report()));

    connect(ui->show_greater_2,SIGNAL(toggled(bool)),this,SLOT(show_fdr_report()));
    connect(ui->show_lesser_2,SIGNAL(toggled(bool)),this,SLOT(show_fdr_report()));


    connect(ui->slice_pos,SIGNAL(valueChanged(int)),this,SLOT(on_subject_list_itemSelectionChanged()));

    connect(ui->view_y,SIGNAL(toggled(bool)),this,SLOT(on_view_x_toggled(bool)));
    connect(ui->view_z,SIGNAL(toggled(bool)),this,SLOT(on_view_x_toggled(bool)));

    connect(ui->zoom,SIGNAL(valueChanged(double)),this,SLOT(on_subject_list_itemSelectionChanged()));

    ui->subject_list->selectRow(0);
    ui->toolBox->setCurrentIndex(1);
    ui->foi_widget->hide();
    on_roi_whole_brain_toggled(true);
    on_rb_multiple_regression_clicked();
    qApp->installEventFilter(this);

    fp_mask.resize(vbc->handle->dim);
    if(!load_cerebrum_mask(fp_mask))
        std::fill(fp_mask.begin(),fp_mask.end(),1);
}

vbc_dialog::~vbc_dialog()
{
    qApp->removeEventFilter(this);
    delete ui;
}

void vbc_dialog::on_rb_percentage_clicked()
{
    ui->percentage_label->show();
    ui->threshold->setDecimals(0);
    ui->explain_label->setText("Mean of the difference divided by the mean ×100%");
}


void vbc_dialog::on_rb_percentile_clicked()
{
    ui->percentage_label->show();
    ui->threshold->setDecimals(0);
    ui->explain_label->setText("Rank in the population in percentile");
}


void vbc_dialog::on_rb_t_stat_clicked()
{
    ui->percentage_label->hide();
    ui->threshold->setDecimals(2);
    ui->explain_label->setText("T statistics");
}

void vbc_dialog::on_rb_mean_dif_clicked()
{
    ui->percentage_label->hide();
    ui->threshold->setDecimals(2);
    ui->explain_label->setText("Mean of the difference");
}

void vbc_dialog::on_rb_beta_clicked()
{
    ui->percentage_label->hide();
    ui->threshold->setDecimals(2);
    ui->explain_label->setText("Beta coefficient");
}


void vbc_dialog::on_rb_individual_analysis_clicked()
{
    ui->individual_demo->show();
    ui->individual_list->show();
    ui->multiple_regression_demo->hide();
    ui->subject_demo->hide();

    ui->foi->hide();
    ui->foi_label->hide();

    ui->rb_percentage->setEnabled(true);
    ui->rb_beta->setEnabled(false);
    ui->rb_t_stat->setEnabled(false);
    ui->rb_percentile->setEnabled(true);
    ui->rb_mean_dif->setEnabled(true);

    ui->rb_percentile->setChecked(true);
    on_rb_percentile_clicked();

}

void vbc_dialog::on_rb_group_difference_clicked()
{
    ui->individual_demo->hide();
    ui->individual_list->hide();
    ui->multiple_regression_demo->show();
    ui->subject_demo->show();

    ui->foi->hide();
    ui->foi_label->hide();

    ui->rb_percentage->setEnabled(true);
    ui->rb_beta->setEnabled(false);
    ui->rb_t_stat->setEnabled(true);
    ui->rb_percentile->setEnabled(false);
    ui->rb_mean_dif->setEnabled(true);

    ui->rb_percentage->setChecked(true);
    on_rb_t_stat_clicked();
}

void vbc_dialog::on_rb_multiple_regression_clicked()
{
    ui->individual_demo->hide();
    ui->individual_list->hide();
    ui->multiple_regression_demo->show();
    ui->subject_demo->show();

    ui->foi->show();
    ui->foi_label->show();

    ui->rb_percentage->setEnabled(true);
    ui->rb_beta->setEnabled(true);
    ui->rb_t_stat->setEnabled(true);
    ui->rb_percentile->setEnabled(false);
    ui->rb_mean_dif->setEnabled(false);

    ui->rb_percentage->setChecked(true);
    on_rb_t_stat_clicked();
}

void vbc_dialog::on_rb_paired_difference_clicked()
{
    ui->individual_demo->hide();
    ui->individual_list->hide();
    ui->multiple_regression_demo->show();
    ui->subject_demo->show();

    ui->foi->hide();
    ui->foi_label->hide();

    ui->rb_percentage->setEnabled(true);
    ui->rb_beta->setEnabled(false);
    ui->rb_t_stat->setEnabled(true);
    ui->rb_percentile->setEnabled(false);
    ui->rb_mean_dif->setEnabled(true);

    ui->rb_percentage->setChecked(true);
    on_rb_t_stat_clicked();
}
bool vbc_dialog::eventFilter(QObject *obj, QEvent *event)
{
    if (event->type() != QEvent::MouseMove || obj->parent() != ui->vbc_view)
        return false;
    QMouseEvent *mouseEvent = static_cast<QMouseEvent*>(event);
    QPointF point = ui->vbc_view->mapToScene(mouseEvent->pos().x(),mouseEvent->pos().y());
    image::vector<3,float> pos;
    pos[0] =  ((float)point.x()) / ui->zoom->value() - 0.5;
    pos[1] =  ((float)point.y()) / ui->zoom->value() - 0.5;
    pos[2] = ui->slice_pos->value();
    if(!vbc->handle->dim.is_valid(pos))
        return true;
    ui->x_pos->setValue(std::floor(pos[0] + 0.5));
    ui->y_pos->setValue(std::floor(pos[1] + 0.5));
    ui->z_pos->setValue(std::floor(pos[2] + 0.5));


    return true;
}
void vbc_dialog::update_subject_list()
{
    ui->subject_list->clear();
    ui->subject_list->setRowCount(vbc->handle->db.num_subjects);
    std::string check_quality,bad_r2;
    for(unsigned int index = 0;index < vbc->handle->db.num_subjects;++index)
    {
        ui->subject_list->setItem(index,0, new QTableWidgetItem(QString(vbc->handle->db.subject_names[index].c_str())));
        ui->subject_list->setItem(index,1, new QTableWidgetItem(QString::number(0)));
        ui->subject_list->setItem(index,2, new QTableWidgetItem(QString::number(vbc->handle->db.R2[index])));
        if(vbc->handle->db.R2[index] < 0.3)
        {
            if(check_quality.empty())
                check_quality = "Low R2 value found in subject(s):";
            std::ostringstream out;
            out << " #" << index+1 << " " << vbc->handle->db.subject_names[index];
            check_quality += out.str();
        }
        if(vbc->handle->db.R2[index] != vbc->handle->db.R2[index])
        {
            if(bad_r2.empty())
                bad_r2 = "Invalid data found in subject(s):";
            std::ostringstream out;
            out << " #" << index+1 << " " << vbc->handle->db.subject_names[index];
            bad_r2 += out.str();
        }

    }
    if(!check_quality.empty())
        QMessageBox::information(this,"Warning",check_quality.c_str());
    if(!bad_r2.empty())
        QMessageBox::information(this,"Warning",bad_r2.c_str());
}

void vbc_dialog::show_fdr_report()
{
    ui->fdr_dist->clearGraphs();
    std::vector<std::vector<float> > vbc_data;
    char legends[4][60] = {"greater","lesser"};
    std::vector<const char*> legend;

    if(ui->show_greater_2->isChecked())
    {
        vbc_data.push_back(vbc->fdr_greater);
        legend.push_back(legends[0]);
    }
    if(ui->show_lesser_2->isChecked())
    {
        vbc_data.push_back(vbc->fdr_lesser);
        legend.push_back(legends[1]);
    }


    QPen pen;
    QColor color[4];
    color[0] = QColor(20,20,255,255);
    color[1] = QColor(255,20,20,255);
    for(unsigned int i = 0; i < vbc_data.size(); ++i)
    {
        QVector<double> x(vbc_data[i].size());
        QVector<double> y(vbc_data[i].size());
        for(unsigned int j = 0;j < vbc_data[i].size();++j)
        {
            x[j] = (float)j;
            y[j] = vbc_data[i][j];
        }
        ui->fdr_dist->addGraph();
        pen.setColor(color[i]);
        pen.setWidth(2);
        ui->fdr_dist->graph()->setLineStyle(QCPGraph::lsLine);
        ui->fdr_dist->graph()->setPen(pen);
        ui->fdr_dist->graph()->setData(x, y);
        ui->fdr_dist->graph()->setName(QString(legend[i]));
    }
    ui->fdr_dist->xAxis->setLabel("mm");
    ui->fdr_dist->yAxis->setLabel("FDR");
    ui->fdr_dist->xAxis->setRange(2,ui->span_to->value());
    ui->fdr_dist->yAxis->setRange(0,1.0);
    ui->fdr_dist->xAxis->setGrid(false);
    ui->fdr_dist->yAxis->setGrid(false);
    ui->fdr_dist->xAxis2->setVisible(true);
    ui->fdr_dist->xAxis2->setTicks(false);
    ui->fdr_dist->xAxis2->setTickLabels(false);
    ui->fdr_dist->yAxis2->setVisible(true);
    ui->fdr_dist->yAxis2->setTicks(false);
    ui->fdr_dist->yAxis2->setTickLabels(false);
    ui->fdr_dist->legend->setVisible(ui->view_legend->isChecked());
    QFont legendFont = font();  // start out with MainWindow's font..
    legendFont.setPointSize(8); // and make a bit smaller for legend
    ui->fdr_dist->legend->setFont(legendFont);
    ui->fdr_dist->legend->setPositionStyle(QCPLegend::psTopRight);
    ui->fdr_dist->legend->setBrush(QBrush(QColor(255,255,255,230)));
    ui->fdr_dist->replot();

}

void vbc_dialog::show_report()
{
    if(vbc->subject_greater_null.empty())
        return;
    ui->null_dist->clearGraphs();
    std::vector<std::vector<unsigned int> > vbc_data;
    char legends[4][60] = {"permuted greater","permuted lesser","nonpermuted greater","nonpermuted lesser"};
    std::vector<const char*> legend;

    if(ui->show_null_greater->isChecked())
    {
        vbc_data.push_back(vbc->subject_greater_null);
        legend.push_back(legends[0]);
    }
    if(ui->show_null_lesser->isChecked())
    {
        vbc_data.push_back(vbc->subject_lesser_null);
        legend.push_back(legends[1]);
    }
    if(ui->show_greater->isChecked())
    {
        vbc_data.push_back(vbc->subject_greater);
        legend.push_back(legends[2]);
    }
    if(ui->show_lesser->isChecked())
    {
        vbc_data.push_back(vbc->subject_lesser);
        legend.push_back(legends[3]);
    }

    // normalize
    float max_y1 = *std::max_element(vbc->subject_greater_null.begin(),vbc->subject_greater_null.end());
    float max_y2 = *std::max_element(vbc->subject_lesser_null.begin(),vbc->subject_lesser_null.end());
    float max_y3 = *std::max_element(vbc->subject_greater.begin(),vbc->subject_greater.end());
    float max_y4 = *std::max_element(vbc->subject_lesser.begin(),vbc->subject_lesser.end());


    if(vbc_data.empty())
        return;

    unsigned int x_size = 0;
    for(unsigned int i = 0;i < vbc_data.size();++i)
        x_size = std::max<unsigned int>(x_size,vbc_data[i].size()-1);
    if(x_size == 0)
        return;
    QVector<double> x(x_size);
    std::vector<QVector<double> > y(vbc_data.size());
    for(unsigned int i = 0;i < vbc_data.size();++i)
        y[i].resize(x_size);

    // tracks length is at least 2 mm, so skip length < 2
    for(unsigned int j = 2;j < x_size && j < vbc_data[0].size();++j)
    {
        x[j-2] = (float)j;
        for(unsigned int i = 0; i < vbc_data.size(); ++i)
            y[i][j-2] = vbc_data[i][j];
    }

    QPen pen;
    QColor color[4];
    color[0] = QColor(20,20,255,255);
    color[1] = QColor(255,20,20,255);
    color[2] = QColor(20,255,20,255);
    color[3] = QColor(20,255,255,255);
    for(unsigned int i = 0; i < vbc_data.size(); ++i)
    {
        ui->null_dist->addGraph();
        pen.setColor(color[i]);
        pen.setWidth(2);
        ui->null_dist->graph()->setLineStyle(QCPGraph::lsLine);
        ui->null_dist->graph()->setPen(pen);
        ui->null_dist->graph()->setData(x, y[i]);
        ui->null_dist->graph()->setName(QString(legend[i]));
    }

    ui->null_dist->xAxis->setLabel("mm");
    ui->null_dist->yAxis->setLabel("count");
    ui->null_dist->xAxis->setRange(0,ui->span_to->value());
    ui->null_dist->yAxis->setRange(0,std::max<float>(std::max<float>(max_y1,max_y2),std::max<float>(max_y3,max_y4))*1.1);
    ui->null_dist->xAxis->setGrid(false);
    ui->null_dist->yAxis->setGrid(false);
    ui->null_dist->xAxis2->setVisible(true);
    ui->null_dist->xAxis2->setTicks(false);
    ui->null_dist->xAxis2->setTickLabels(false);
    ui->null_dist->yAxis2->setVisible(true);
    ui->null_dist->yAxis2->setTicks(false);
    ui->null_dist->yAxis2->setTickLabels(false);
    ui->null_dist->legend->setVisible(ui->view_legend->isChecked());
    QFont legendFont = font();  // start out with MainWindow's font..
    legendFont.setPointSize(8); // and make a bit smaller for legend
    ui->null_dist->legend->setFont(legendFont);
    ui->null_dist->legend->setPositionStyle(QCPLegend::psTopRight);
    ui->null_dist->legend->setBrush(QBrush(QColor(255,255,255,230)));
    ui->null_dist->replot();
}

void vbc_dialog::show_dis_table(void)
{
    ui->dist_table->setRowCount(100);
    for(unsigned int index = 0;index < vbc->fdr_greater.size()-1;++index)
    {
        ui->dist_table->setItem(index,0, new QTableWidgetItem(QString::number(index + 1)));
        ui->dist_table->setItem(index,1, new QTableWidgetItem(QString::number(vbc->fdr_greater[index+1])));
        ui->dist_table->setItem(index,2, new QTableWidgetItem(QString::number(vbc->fdr_lesser[index+1])));
        ui->dist_table->setItem(index,3, new QTableWidgetItem(QString::number(vbc->subject_greater_null[index+1])));
        ui->dist_table->setItem(index,4, new QTableWidgetItem(QString::number(vbc->subject_lesser_null[index+1])));
        ui->dist_table->setItem(index,5, new QTableWidgetItem(QString::number(vbc->subject_greater[index+1])));
        ui->dist_table->setItem(index,6, new QTableWidgetItem(QString::number(vbc->subject_lesser[index+1])));
    }
    ui->dist_table->selectRow(0);
}

void vbc_dialog::on_subject_list_itemSelectionChanged()
{
    if(ui->view_x->isChecked())
        ui->x_pos->setValue(ui->slice_pos->value());
    if(ui->view_y->isChecked())
        ui->y_pos->setValue(ui->slice_pos->value());
    if(ui->view_z->isChecked())
        ui->z_pos->setValue(ui->slice_pos->value());

    image::basic_image<float,2> slice;
    vbc->handle->db.get_subject_slice(ui->subject_list->currentRow(),
                                   ui->view_x->isChecked() ? 0:(ui->view_y->isChecked() ? 1:2),
                                   ui->slice_pos->value(),slice);
    image::normalize(slice);
    image::color_image color_slice(slice.geometry());
    std::copy(slice.begin(),slice.end(),color_slice.begin());
    QImage qimage((unsigned char*)&*color_slice.begin(),color_slice.width(),color_slice.height(),QImage::Format_RGB32);
    vbc_slice_image = qimage.scaled(color_slice.width()*ui->zoom->value(),color_slice.height()*ui->zoom->value());
    if(!ui->view_z->isChecked())
        vbc_slice_image = vbc_slice_image.mirrored();
    vbc_scene.clear();
    vbc_scene.setSceneRect(0, 0, vbc_slice_image.width(),vbc_slice_image.height());
    vbc_scene.setItemIndexMethod(QGraphicsScene::NoIndex);
    vbc_scene.addRect(0, 0, vbc_slice_image.width(),vbc_slice_image.height(),QPen(),vbc_slice_image);
    vbc_slice_pos = ui->slice_pos->value();

    if(ui->toolBox->currentIndex() == 0 && ui->subject_view->currentIndex() == 1)
    {
        std::vector<float> fp;
        float threshold = ui->fp_coverage->value()*image::segmentation::otsu_threshold(image::make_image(vbc->handle->dir.fa[0],vbc->handle->dim));
        vbc->handle->db.get_subject_vector(ui->subject_list->currentRow(),fp,fp_mask,threshold,ui->normalize_fp->isChecked());
        fp_image_buf.clear();
        fp_image_buf.resize(image::geometry<2>(ui->fp_zoom->value()*25,ui->fp_zoom->value()*100));// rotated

        image::minus_constant(fp.begin(),fp.end(),*std::min_element(fp.begin(),fp.end()));
        float max_fp = *std::max_element(fp.begin(),fp.end());
        if(max_fp == 0)
            return;
        image::multiply_constant(fp,(float)fp_image_buf.width()/max_fp);
        std::vector<int> ifp(fp.size());
        std::copy(fp.begin(),fp.end(),ifp.begin());
        image::upper_lower_threshold(ifp,0,(int)fp_image_buf.width()-1);
        unsigned int* base = (unsigned int*)&fp_image_buf[0];
        for(unsigned int i = 0;i < fp_image_buf.height();++i,base += fp_image_buf.width())
        {
            unsigned int from_index = (i)*ifp.size()/fp_image_buf.height();
            unsigned int to_index = (i+1)*ifp.size()/fp_image_buf.height();
            for(++from_index;from_index != to_index;++from_index)
            {
                unsigned int from = ifp[from_index-1];
                unsigned int to = ifp[from_index];
                if(from > to)
                    std::swap(from,to);
                image::add_constant(base+from,base+to,1);
            }
        }
        base = (unsigned int*)&fp_image_buf[0];
        unsigned int max_value = *std::max_element(base,base+fp_image_buf.size());
        for(unsigned int index = 0;index < fp_image_buf.size();++index)
            fp_image_buf[index] = image::rgb_color((unsigned char)(255-std::min<int>(255,(fp_image_buf[index].color*512/max_value))));
        image::swap_xy(fp_image_buf);
        image::flip_y(fp_image_buf);
        QImage fp_image_tmp((unsigned char*)&*fp_image_buf.begin(),fp_image_buf.width(),fp_image_buf.height(),QImage::Format_RGB32);
        fp_image = fp_image_tmp;
        fp_scene.setSceneRect(0, 0, fp_image.width(),fp_image.height());
        fp_scene.clear();
        fp_scene.setItemIndexMethod(QGraphicsScene::NoIndex);
        fp_scene.addRect(0, 0, fp_image.width(),fp_image.height(),QPen(),fp_image);

    }

    if(!fp_dif_map.empty() && fp_dif_map.width() == vbc->handle->db.num_subjects)
    {
        fp_dif_map.resize(image::geometry<2>(vbc->handle->db.num_subjects,vbc->handle->db.num_subjects));
        for(unsigned int index = 0;index < fp_matrix.size();++index)
            fp_dif_map[index] = color_map[fp_matrix[index]*256.0/fp_max_value];

        // line x
        for(unsigned int x_pos = 0,pos = ui->subject_list->currentRow()*vbc->handle->db.num_subjects;x_pos < vbc->handle->db.num_subjects;++x_pos,++pos)
        {
            fp_dif_map[pos][2] = (fp_dif_map[pos][0] >> 1);
            fp_dif_map[pos][2] += 125;
        }
        // line y
        for(unsigned int y_pos = 0,pos = ui->subject_list->currentRow();y_pos < vbc->handle->db.num_subjects;++y_pos,pos += vbc->handle->db.num_subjects)
        {
            fp_dif_map[pos][2] = (fp_dif_map[pos][0] >> 1);
            fp_dif_map[pos][2] += 125;
        }
        on_fp_zoom_valueChanged(0);
    }
}

void vbc_dialog::on_open_files_clicked()
{
    QString file_name = QFileDialog::getOpenFileName(
                                this,
                "Select patients' Connectometry DB",
                work_dir,"Connectometry DB files (*db?fib.gz);;All files (*)" );
    if (file_name.isEmpty())
        return;
    std::shared_ptr<fib_data> handle(new fib_data);
    begin_prog("reading connectometry DB");
    if(!handle->load_from_file(file_name.toStdString().c_str()))
    {
        QMessageBox::information(this,"Error",handle->error_msg.c_str(),0);
        return;
    }
    if(!vbc->handle->db.is_db_compatible(handle->db))
    {
        QMessageBox::information(this,"Error",vbc->handle->error_msg.c_str(),0);
        return;
    }


    model.reset(new stat_model);
    model->init(vbc->handle->db.num_subjects);
    QStringList name_list;
    file_names.clear();
    for(unsigned int i = 0;i < handle->db.num_subjects;++i)
    {
        name_list.push_back(handle->db.subject_names[i].c_str());
        file_names.push_back(file_name.toStdString() + "." + handle->db.subject_names[i]);
    }
    handle->db.read_subject_qa(individual_data);
    ui->threshold->setValue(2);
    model->type = 2;
    ((QStringListModel*)ui->individual_list->model())->setStringList(name_list);
    ui->run->setEnabled(true);
    on_suggest_threshold_clicked();
}


void vbc_dialog::on_open_mr_files_clicked()
{
    QString filename = QFileDialog::getOpenFileName(
                this,
                "Open demographics",
                work_dir,
                "Text file (*.txt);;All files (*)");
    if(filename.isEmpty())
        return;
    load_demographic_file(filename);
}

bool vbc_dialog::load_demographic_file(QString filename)
{
    model.reset(new stat_model);
    model->init(vbc->handle->db.num_subjects);
    file_names.clear();
    file_names.push_back(filename.toLocal8Bit().begin());

    std::vector<std::string> items;
    std::ifstream in(filename.toLocal8Bit().begin());
    if(!in)
    {
        if(gui)
            QMessageBox::information(this,"Error",QString("Cannot find the demographic file."));
        else
            std::cout << "cannot find the demographic file at" << filename.toLocal8Bit().begin() << std::endl;
        return false;
    }
    std::copy(std::istream_iterator<std::string>(in),
              std::istream_iterator<std::string>(),std::back_inserter(items));

    if(ui->rb_multiple_regression->isChecked())
    {
        unsigned int feature_count = items.size()/(vbc->handle->db.num_subjects+1);
        if(feature_count*(vbc->handle->db.num_subjects+1) != items.size())
        {
            if(gui)
            {
                int result = QMessageBox::information(this,"Warning",QString("Subject number mismatch. text file has %1 elements while database has %2 subjects. Try to match the data?").
                                                                arg(items.size()).arg(vbc->handle->db.num_subjects),QMessageBox::Yes|QMessageBox::No);
                if(result == QMessageBox::No)
                    return false;
                items.resize(feature_count*(vbc->handle->db.num_subjects+1));
            }
            else
            {
                std::cout << "subject number mismatch in the demographic file" <<std::endl;
                return false;
            }
        }
        bool add_age_and_sex = false;
        std::vector<unsigned int> age(vbc->handle->db.num_subjects),sex(vbc->handle->db.num_subjects);
        if((QString(vbc->handle->db.subject_names[0].c_str()).contains("_M0") || QString(vbc->handle->db.subject_names[0].c_str()).contains("_F0")) &&
            QString(vbc->handle->db.subject_names[0].c_str()).contains("Y_") && gui &&
            QMessageBox::information(this,"Connectomtetry aanalysis","Pull age and sex (1 = male, 0 = female) information from connectometry db?",QMessageBox::Yes|QMessageBox::No) == QMessageBox::Yes)
            {
                add_age_and_sex = true;
                for(unsigned int index = 0;index < vbc->handle->db.num_subjects;++index)
                {
                    QString name = vbc->handle->db.subject_names[index].c_str();
                    if(name.contains("_M0"))
                        sex[index] = 1;
                    else
                        if(name.contains("_F0"))
                            sex[index] = 0;
                        else
                        {
                            sex[index] = ui->missing_value->value();
                            ui->missing_data_checked->setChecked(true);
                        }
                    int pos = name.indexOf("Y_")-2;
                    if(pos <= 0)
                        continue;
                    bool okay;
                    age[index] = name.mid(pos,2).toInt(&okay);
                    if(!okay)
                    {
                        age[index] = ui->missing_value->value();
                        ui->missing_data_checked->setChecked(true);
                    }
                }
            }

        std::vector<double> X;
        for(unsigned int i = 0,index = 0;i < vbc->handle->db.num_subjects;++i)
        {
            bool ok = false;
            X.push_back(1); // for the intercep
            if(add_age_and_sex)
            {
                X.push_back(age[i]);
                X.push_back(sex[i]);
            }
            for(unsigned int j = 0;j < feature_count;++j,++index)
            {
                X.push_back(QString(items[index+feature_count].c_str()).toDouble(&ok));
                if(!ok)
                {
                    if(gui)
                    QMessageBox::information(this,"Error",QString("Cannot parse '") +
                                             QString(items[index+feature_count].c_str()) +
                                             QString("' at subject%1 feature%2.").arg(i+1).arg(j+1),0);
                    else
                        std::cout << "invalid demographic file: cannot parse " << items[index+feature_count] << std::endl;
                    return false;
                }
            }
        }
        model->type = 1;
        model->X = X;
        model->feature_count = feature_count+1; // additional one for intercept
        QStringList t;
        t << "Subject ID";
        if(add_age_and_sex)
        {
            model->feature_count += 2;
            t << "Age";
            t << "Sex";
        }
        for(unsigned int index = 0;index < feature_count;++index)
        {
            std::replace(items[index].begin(),items[index].end(),'/','_');
            std::replace(items[index].begin(),items[index].end(),'\\','_');
            t << items[index].c_str();
        }
        ui->foi->clear();
        ui->foi->addItems(t);
        ui->foi->removeItem(0);
        ui->foi->setCurrentIndex(ui->foi->count()-1);
        ui->foi_widget->show();
        ui->subject_demo->clear();
        ui->subject_demo->setColumnCount(t.size());
        ui->subject_demo->setHorizontalHeaderLabels(t);
        ui->subject_demo->setRowCount(vbc->handle->db.num_subjects);
        for(unsigned int row = 0,index = 0;row < ui->subject_demo->rowCount();++row)
        {
            ui->subject_demo->setItem(row,0,new QTableWidgetItem(QString(vbc->handle->db.subject_names[row].c_str())));
            ++index;// skip intercep
            for(unsigned int col = 1;col < ui->subject_demo->columnCount();++col,++index)
                ui->subject_demo->setItem(row,col,new QTableWidgetItem(QString::number(model->X[index])));
        }
        ui->missing_data_checked->setChecked(std::find(X.begin(),X.end(),ui->missing_value->value()) != X.end());
    }
    if(ui->rb_group_difference->isChecked())
    {
        if(vbc->handle->db.num_subjects != items.size() &&
           vbc->handle->db.num_subjects+1 != items.size())
        {
            if(gui)
                QMessageBox::information(this,"Warning",
                                     QString("Subject number mismatch. text file=%1 database=%2").
                                     arg(items.size()).arg(vbc->handle->db.num_subjects));
            else
                std::cout << "invalid demographic file: subject number mismatch." << std::endl;
            return false;
        }
        if(vbc->handle->db.num_subjects+1 == items.size())
            items.erase(items.begin());

        std::vector<int> label;
        for(unsigned int i = 0;i < vbc->handle->db.num_subjects;++i)
        {
            bool ok = false;
            label.push_back(QString(items[i].c_str()).toInt(&ok));
            if(!ok)
            {
                if(gui)
                    QMessageBox::information(this,"Error",QString("Cannot parse ") +
                                             QString(items[i].c_str()) +
                                             QString(" at subject%1").arg(i+1),0);
                else
                    std::cout << "invalid demographic file: cannot parse " << items[i] << std::endl;
                return false;
            }
        }

        model->type = 0;
        model->label = label;
        ui->subject_demo->clear();
        ui->subject_demo->setColumnCount(2);
        ui->subject_demo->setHorizontalHeaderLabels(QStringList() << "Subject ID" << "Group ID");
        ui->subject_demo->setRowCount(vbc->handle->db.num_subjects);
        for(unsigned int row = 0;row < ui->subject_demo->rowCount();++row)
        {
            ui->subject_demo->setItem(row,0,new QTableWidgetItem(QString(vbc->handle->db.subject_names[row].c_str())));
            ui->subject_demo->setItem(row,1,new QTableWidgetItem(QString::number(model->label[row])));
        }
    }
    if(ui->rb_paired_difference->isChecked())
    {
        if(vbc->handle->db.num_subjects != items.size() &&
           vbc->handle->db.num_subjects+1 != items.size())
        {
            if(gui)
                QMessageBox::information(this,"Warning",
                                     QString("Subject number mismatch. text file=%1 database=%2").
                                     arg(items.size()).arg(vbc->handle->db.num_subjects));
            else
                std::cout << "invalid demographic file: subject number mismatch." << std::endl;
            return false;
        }
        if(vbc->handle->db.num_subjects+1 == items.size())
            items.erase(items.begin());

        std::vector<int> label;
        for(unsigned int i = 0;i < vbc->handle->db.num_subjects;++i)
        {
            bool ok = false;
            label.push_back(QString(items[i].c_str()).toInt(&ok));
            if(!ok)
            {
                if(gui)
                    QMessageBox::information(this,"Error",QString("Cannot parse ") +
                                             QString(items[i].c_str()) +
                                             QString(" at subject%1").arg(i+1),0);
                else
                    std::cout << "invalid demographic file: cannot parse " << items[i] << std::endl;
                return false;
            }
        }

        model->type = 3;
        model->subject_index.clear();
        model->paired.clear();
        for(unsigned int i = 0;i < label.size() && i < vbc->handle->db.num_subjects;++i)
            if(label[i] > 0)
            {
                for(unsigned int j = 0;j < label.size() && j < vbc->handle->db.num_subjects;++j)
                    if(label[j] == -label[i])
                    {
                        model->subject_index.push_back(i);
                        model->paired.push_back(j);
                    } 
            }
        ui->subject_demo->clear();
        ui->subject_demo->setColumnCount(2);
        ui->subject_demo->setHorizontalHeaderLabels(QStringList() << "Subject ID" << "Matched ID");
        ui->subject_demo->setRowCount(model->subject_index.size());
        for(unsigned int row = 0;row < ui->subject_demo->rowCount();++row)
        {
            ui->subject_demo->setItem(row,0,new QTableWidgetItem(QString(vbc->handle->db.subject_names[model->subject_index[row]].c_str())));
            ui->subject_demo->setItem(row,1,new QTableWidgetItem(QString(vbc->handle->db.subject_names[model->paired[row]].c_str())));
        }
    }



    if(!model->pre_process())
    {
        if(gui)
            QMessageBox::information(this,"Error","Invalid subjet information for statistical analysis",0);
        else
            std::cout << "invalid subjet information for statistical analysis" << std::endl;
        ui->run->setEnabled(false);
        return false;
    }

    ui->run->setEnabled(true);
    on_suggest_threshold_clicked();
    return true;
}

void vbc_dialog::setup_model(stat_model& m)
{
    m = *(model.get());
    if(ui->rb_multiple_regression->isChecked())
        m.study_feature = ui->foi->currentIndex()+1;
    if(ui->rb_beta->isChecked())
        m.threshold_type = stat_model::beta;
    if(ui->rb_percentage->isChecked())
        m.threshold_type = stat_model::percentage;
    if(ui->rb_percentile->isChecked())
        m.threshold_type = stat_model::percentile;
    if(ui->rb_t_stat->isChecked())
        m.threshold_type = stat_model::t;
    if(ui->rb_mean_dif->isChecked())
        m.threshold_type = stat_model::mean_dif;
    if(ui->missing_data_checked->isChecked())
        m.remove_missing_data(ui->missing_value->value());
}

void vbc_dialog::on_suggest_threshold_clicked()
{
    if(!ui->run->isEnabled())
        return;
    result_fib.reset(new connectometry_result);
    stat_model info;
    setup_model(info);
    bool terminated = false;
    calculate_spm(vbc->handle,*result_fib.get(),info,
                  vbc->fiber_threshold,ui->normalize_qa->isChecked(),terminated);
    std::vector<float> values;
    values.reserve(vbc->handle->dim.size()/8);
    for(unsigned int index = 0;index < vbc->handle->dim.size();++index)
        if(vbc->handle->dir.fa[0][index] > vbc->fiber_threshold)
            values.push_back(result_fib->lesser_ptr[0][index] == 0 ?
                result_fib->greater_ptr[0][index] :  result_fib->lesser_ptr[0][index]);
    float max_value = *std::max_element(values.begin(),values.end());
    if(ui->rb_percentage->isChecked() || ui->rb_percentile->isChecked())
    {
        ui->range_label->setText(QString("max:%2").arg(max_value*100));
        ui->threshold->setMaximum(max_value*100);
        ui->threshold->setValue(image::segmentation::otsu_threshold(values)*100);
    }
    else
    {
        ui->range_label->setText(QString("max:%2").arg(max_value));
        ui->threshold->setMaximum(max_value);
        ui->threshold->setValue(image::segmentation::otsu_threshold(values));
    }
}


void vbc_dialog::calculate_FDR(void)
{
    vbc->calculate_FDR();
    show_report();
    show_dis_table();
    show_fdr_report();

    if(vbc->progress == 100)
    {
        vbc->wait();// make sure that all threads done
        timer->stop();
        vbc->save_tracks_files(saved_file_name); // prepared track recognition results
    }
    report.clear();
    if(!vbc->handle->report.empty())
        report = vbc->handle->report.c_str();
    if(!vbc->report.empty())
        report += vbc->report.c_str();

    if(ui->rb_individual_analysis->isChecked())
        {
            std::ostringstream out;
            out << " The connectometry analysis identified "
                << (vbc->fdr_greater[vbc->length_threshold]>0.5 || !vbc->has_greater_result ? "no track": vbc->greater_tracks_result.c_str())
                << " with increased connectivity (FDR="
                << vbc->fdr_greater[vbc->length_threshold] << ") "
                << "and "
                << (vbc->fdr_lesser[vbc->length_threshold]>0.5 || !vbc->has_lesser_result ? "no track": vbc->lesser_tracks_result.c_str())
                << " with decreased connectivity (FDR="
                << vbc->fdr_lesser[vbc->length_threshold] << ").";
            report += out.str().c_str();
        }
        if(ui->rb_multiple_regression->isChecked())
        {
            std::ostringstream out;
            out << " The connectometry analysis identified "
                << (vbc->fdr_greater[vbc->length_threshold]>0.5 || !vbc->has_greater_result ? "no track": vbc->greater_tracks_result.c_str())
                << " with increased connectivity related to "
                << ui->foi->currentText().toLocal8Bit().begin() << " (FDR="
                << vbc->fdr_greater[vbc->length_threshold] << ") "
                << "and "
                << (vbc->fdr_lesser[vbc->length_threshold]>0.5 || !vbc->has_lesser_result ? "no track": vbc->lesser_tracks_result.c_str())
                << " with decreased connectivity related to "
                << ui->foi->currentText().toLocal8Bit().begin() << " (FDR="
                << vbc->fdr_lesser[vbc->length_threshold] << ").";
            report += out.str().c_str();
        }
        if(ui->rb_group_difference->isChecked() || ui->rb_paired_difference->isChecked())
        {
            std::ostringstream out;
            out << " The connectometry analysis identified "
                << (vbc->fdr_greater[vbc->length_threshold]>0.5 || !vbc->has_greater_result ? "no track": vbc->greater_tracks_result.c_str())
                << " with increased connectivity in group 0 (FDR="
                << vbc->fdr_greater[vbc->length_threshold] << ") "
                << "and "
                << (vbc->fdr_lesser[vbc->length_threshold]>0.5 || !vbc->has_lesser_result ? "no track": vbc->lesser_tracks_result.c_str())
                << " with increased connectivity in group 1 (FDR="
                << vbc->fdr_lesser[vbc->length_threshold] << ").";
            report += out.str().c_str();
        }

    ui->textBrowser->setText(report);

    if(vbc->progress == 100)
    {
        // save report in text
        std::ofstream out((vbc->trk_file_names[0]+".report.txt").c_str());
        out << report.toLocal8Bit().begin() << std::endl;
        // save pdf plot and value txt
        ui->show_null_greater->setChecked(true);
        ui->show_greater->setChecked(true);
        ui->show_null_lesser->setChecked(false);
        ui->show_lesser->setChecked(false);
        ui->null_dist->saveBmp((vbc->trk_file_names[0]+".greater.dist.bmp").c_str(),300,300,3);

        ui->show_null_greater->setChecked(false);
        ui->show_greater->setChecked(false);
        ui->show_null_lesser->setChecked(true);
        ui->show_lesser->setChecked(true);
        ui->null_dist->saveBmp((vbc->trk_file_names[0]+".lesser.dist.bmp").c_str(),300,300,3);

        ui->show_greater_2->setChecked(true);
        ui->show_lesser_2->setChecked(false);
        ui->fdr_dist->saveBmp((vbc->trk_file_names[0]+".greater.fdr.bmp").c_str(),300,300,3);

        ui->show_greater_2->setChecked(false);
        ui->show_lesser_2->setChecked(true);
        ui->fdr_dist->saveBmp((vbc->trk_file_names[0]+".lesser.fdr.bmp").c_str(),300,300,3);


        // restore all checked status
        ui->show_null_greater->setChecked(true);
        ui->show_greater->setChecked(true);
        ui->show_greater_2->setChecked(true);


        ui->fdr_dist->saveTxt((vbc->trk_file_names[0]+".fdr_value.txt").c_str());
        ui->null_dist->saveTxt((vbc->trk_file_names[0]+".dist_value.txt").c_str());

        if(gui)
        {
            if(vbc->has_greater_result || vbc->has_lesser_result)
                QMessageBox::information(this,"Finished","Trk files saved.",0);
            else
                QMessageBox::information(this,"Finished","No significant finding.",0);
        }
        else
        {
            if(vbc->has_greater_result || vbc->has_lesser_result)
                std::cout << "trk files saved" << std::endl;
            else
                std::cout << "no significant finding" << std::endl;
        }
        ui->run->setText("Run");
        ui->progressBar->setValue(100);
        timer.reset(0);
    }
    else
        ui->progressBar->setValue(vbc->progress);
}
void vbc_dialog::on_run_clicked()
{
    if(ui->run->text() == "Stop")
    {
        vbc->clear();
        timer->stop();
        timer.reset(0);
        ui->progressBar->setValue(0);
        ui->run->setText("Run");
        return;
    }
    ui->run->setText("Stop");
    ui->span_to->setValue(80);
    vbc->seeding_density = ui->seeding_density->value();
    vbc->trk_file_names = file_names;
    vbc->normalize_qa = ui->normalize_qa->isChecked();
    vbc->output_resampling = ui->output_resampling->isChecked();
    vbc->length_threshold = ui->length_threshold->value();
    vbc->track_trimming = ui->track_trimming->value();
    vbc->individual_data.clear();
    vbc->tracking_threshold = (ui->rb_percentage->isChecked() || ui->rb_percentile->isChecked() ?
                                   (float)ui->threshold->value()*0.01 : ui->threshold->value());

    vbc->model.reset(new stat_model);
    setup_model(*vbc->model.get());


    std::ostringstream out;
    std::string parameter_str;
    {
        std::ostringstream out;
        if(ui->normalize_qa->isChecked())
            out << ".nqa";
        char threshold_type[5][11] = {"percentage","t","beta","percentile","mean_dif"};
        out << ".length" << ui->length_threshold->value();
        out << ".s" << ui->seeding_density->value();
        out << ".p" << ui->mr_permutation->value();
        out << "." << threshold_type[vbc->model->threshold_type];
        out << "." << ui->threshold->value();

        parameter_str = out.str();
    }

    if(ui->rb_individual_analysis->isChecked())
    {
        vbc->tracking_threshold = 1.0-vbc->tracking_threshold;
        vbc->individual_data = individual_data;
        vbc->individual_data_sd.resize(vbc->individual_data.size());
        for(unsigned int index = 0;index < vbc->individual_data.size();++index)
        {
            vbc->individual_data_sd[index] = image::standard_deviation(vbc->individual_data[index].begin(),vbc->individual_data[index].end());
            if(vbc->individual_data_sd[index] == 0.0)
                vbc->individual_data_sd[index] = 1.0;
            else
                vbc->individual_data_sd[index] = 1.0/vbc->individual_data_sd[index];
        }
        out << "\nDiffusion MRI connectometry (Yeh et al. NeuroImage 125 (2016): 162-171) was conducted to identify affected pathways in "
            << vbc->individual_data.size() << " study patients.";
        out << " The diffusion data of the patients were compared with "
            << vbc->handle->db.num_subjects << " normal subjects.";
        for(unsigned int index = 0;index < vbc->trk_file_names.size();++index)
            vbc->trk_file_names[index] += parameter_str;
    }
    if(ui->rb_group_difference->isChecked())
    {
        out << "\nDiffusion MRI connectometry (Yeh et al. NeuroImage 125 (2016): 162-171) was conducted to compare group differences in a total of "
            << vbc->model->subject_index.size() << " subjects.";
        vbc->trk_file_names[0] += parameter_str;
        vbc->trk_file_names[0] += ".group";

    }
    if(ui->rb_paired_difference->isChecked())
    {
        out << "\nDiffusion MRI connectometry (Yeh et al. NeuroImage 125 (2016): 162-171) was conducted to compare paired group differences in a total of "
            << vbc->model->subject_index.size() << " pairs.";
        vbc->trk_file_names[0] += parameter_str;
        vbc->trk_file_names[0] += ".paired";
    }
    if(ui->rb_multiple_regression->isChecked())
    {
        out << "\nDiffusion MRI connectometry (Yeh et al. NeuroImage 125 (2016): 162-171) was used to study the effect of "
            << ui->foi->currentText().toStdString()
            << ". A multiple regression model was used to consider ";
        for(unsigned int index = 0;index < ui->foi->count();++index)
        {
            if(index && ui->foi->count() > 2)
                out << ",";
            out << " ";
            if(ui->foi->count() >= 2 && index+1 == ui->foi->count())
                out << "and ";
            out << ui->foi->itemText(index).toStdString();
        }
        out << " in a total of "
            << vbc->model->subject_index.size()
            << " subjects. ";

        vbc->trk_file_names[0] += parameter_str;
        vbc->trk_file_names[0] += ".mr.";
        vbc->trk_file_names[0] += ui->foi->currentText().toLower().toLocal8Bit().begin();
    }
    if(ui->normalize_qa->isChecked())
        out << " The SDF was normalized.";
    if(ui->rb_percentage->isChecked())
        out << " A percentage threshold of " << ui->threshold->value();
    if(ui->rb_percentile->isChecked())
        out << " A percentile threshold of " << ui->threshold->value();
    if(ui->rb_beta->isChecked())
        out << " A beta coefficient threshold of " << ui->threshold->value();
    if(ui->rb_t_stat->isChecked())
        out << " A t threshold of " << ui->threshold->value();
    if(ui->rb_mean_dif->isChecked())
        out << " A mean difference threshold of " << ui->threshold->value();
    out << " was assigned to select local connectomes, and the local connectomes were tracked using a deterministic fiber tracking algorithm (Yeh et al. PLoS ONE 8(11): e80713, 2013).";

    // load region
    if(!ui->roi_whole_brain->isChecked() && !roi_list.empty())
    {
        out << " The tracking algorithm used ";
        const char roi_type_name[5][20] = {"region of interst","region of avoidance","ending region","seeding region","terminating region"};
        const char roi_type_name2[5][5] = {"roi","roa","end","seed"};
        vbc->roi_list = roi_list;
        vbc->roi_type.resize(roi_list.size());
        for(unsigned int index = 0;index < roi_list.size();++index)
        {
            if(index && roi_list.size() > 2)
                out << ",";
            out << " ";
            if(roi_list.size() >= 2 && index+1 == roi_list.size())
                out << "and ";
            out << ui->roi_table->item(index,0)->text().toStdString() << " as the " << roi_type_name[ui->roi_table->item(index,2)->text().toInt()];
            QString name = ui->roi_table->item(index,0)->text();
            name = name.replace('?','_');
            name = name.replace(':','_');
            name = name.replace('/','_');
            name = name.replace('\\','_');
            vbc->roi_type[index] = ui->roi_table->item(index,2)->text().toInt();
            for(unsigned int index = 0;index < vbc->trk_file_names.size();++index)
            {
                vbc->trk_file_names[index] += ".";
                vbc->trk_file_names[index] += roi_type_name2[vbc->roi_type.front()];
                vbc->trk_file_names[index] += ".";
                vbc->trk_file_names[index] += name.toStdString();
            }
        }
        out << ".";
    }
    else
    {
        vbc->roi_list.clear();
        vbc->roi_type.clear();
    }

    if(vbc->track_trimming)
        out << " Track trimming was conducted with " << vbc->track_trimming << " iterations.";

    if(vbc->output_resampling)
        out << " All tracks generated from bootstrap resampling were included.";

    out << " A length threshold of " << ui->length_threshold->value() << " mm was used to select tracks.";
    out << " The seeding density was " <<
            ui->seeding_density->value() << " seed(s) per mm3.";

    out << " To estimate the false discovery rate, a total of "
        << ui->mr_permutation->value()
        << " randomized permutations were applied to the group label to obtain the null distribution of the track length.";

    vbc->report = out.str().c_str();
    vbc->run_permutation(ui->multithread->value(),ui->mr_permutation->value());
    timer.reset(new QTimer(this));
    timer->setInterval(1000);
    connect(timer.get(), SIGNAL(timeout()), this, SLOT(calculate_FDR()));
    timer->start();
}

void vbc_dialog::on_save_name_list_clicked()
{
    QString filename = QFileDialog::getSaveFileName(
                this,
                "Save name list",
                db_file_name + ".name.txt",
                "Report file (*.txt);;All files (*)");
    if(filename.isEmpty())
        return;
    std::ofstream out(filename.toLocal8Bit().begin());
    for(unsigned int index = 0;index < vbc->handle->db.num_subjects;++index)
        out << vbc->handle->db.subject_names[index] << std::endl;
}

void vbc_dialog::on_show_result_clicked()
{
    std::shared_ptr<fib_data> new_data(new fib_data);
    *(new_data.get()) = *(vbc->handle);
    if(!report.isEmpty())
    {
        std::ostringstream out;
        out << report.toLocal8Bit().begin();
        new_data->report += out.str();
    }
    stat_model* cur_model = vbc->model.get() ? vbc->model.get():model.get();
    if(cur_model->type != 2) // not individual
    {
        char threshold_type[5][11] = {"percentage","t","beta","percentile","mean_dif"};
        result_fib.reset(new connectometry_result);
        stat_model info;
        info.resample(*cur_model,false,false);
        vbc->calculate_spm(*result_fib.get(),info,vbc->normalize_qa);
        new_data->view_item.push_back(item());
        new_data->view_item.back().name = threshold_type[vbc->model->threshold_type];
        new_data->view_item.back().name += "-";
        new_data->view_item.back().image_data = image::make_image(result_fib->lesser_ptr[0],new_data->dim);
        new_data->view_item.back().set_scale(result_fib->lesser_ptr[0],
                                             result_fib->lesser_ptr[0]+new_data->dim.size());
        new_data->view_item.push_back(item());
        new_data->view_item.back().name = threshold_type[vbc->model->threshold_type];
        new_data->view_item.back().name += "+";
        new_data->view_item.back().image_data = image::make_image(result_fib->greater_ptr[0],new_data->dim);
        new_data->view_item.back().set_scale(result_fib->greater_ptr[0],
                                             result_fib->greater_ptr[0]+new_data->dim.size());

    }
    tracking_window* current_tracking_window = new tracking_window(this,new_data);
    current_tracking_window->setAttribute(Qt::WA_DeleteOnClose);
    current_tracking_window->setWindowTitle(saved_file_name.front().c_str());
    current_tracking_window->showNormal();
    current_tracking_window->tractWidget->delete_all_tract();
    QStringList filenames;
    for(unsigned int index = 0;index < saved_file_name.size();++index)
        if(QFileInfo(saved_file_name[index].c_str()).exists())
            filenames << saved_file_name[index].c_str();
    if(!filenames.empty())
        current_tracking_window->tractWidget->load_tracts(filenames);

}

void vbc_dialog::on_roi_whole_brain_toggled(bool checked)
{
    ui->roi_table->setEnabled(!checked);
    ui->load_roi_from_atlas->setEnabled(!checked);
    ui->clear_all_roi->setEnabled(!checked);
    ui->load_roi_from_file->setEnabled(!checked);
}


void vbc_dialog::on_remove_subject_clicked()
{
    if(ui->subject_list->currentRow() >= 0 && vbc->handle->db.num_subjects > 1)
    {
        unsigned int index = ui->subject_list->currentRow();
        vbc->handle->db.remove_subject(index);
        if(model.get())
            model->remove_subject(index);
        if(index < ui->subject_demo->rowCount())
            ui->subject_demo->removeRow(index);
        if(index < ui->subject_list->rowCount())
            ui->subject_list->removeRow(index);
    }

}

void vbc_dialog::on_toolBox_currentChanged(int index)
{
    if(index > 1 && !ui->run->isEnabled())
    {
        QMessageBox::information(this,"Missing information","Please provide patient information in STEP1 before running connectometry",0);
        ui->toolBox->setCurrentIndex(1);
    }
}

void vbc_dialog::on_x_pos_valueChanged(int arg1)
{
    // show data
    std::vector<double> vbc_data;
    vbc->handle->db.get_data_at(
            image::pixel_index<3>(ui->x_pos->value(),
                                  ui->y_pos->value(),
                                  ui->z_pos->value(),
                                  vbc->handle->dim).index(),0,vbc_data,vbc->normalize_qa);
    if(vbc_data.empty())
        return;
    if(ui->run->isEnabled() && ui->rb_multiple_regression->isChecked())
    {
        model->select(vbc_data,vbc_data);
        if(vbc_data.empty())
            return;
        QVector<double> variables(vbc_data.size());
        for(unsigned int i = 0;i < vbc_data.size();++i)
            variables[i] = model->X[i*model->feature_count+ui->foi->currentIndex()+1];

        QVector<double> y(vbc_data.size());
        std::copy(vbc_data.begin(),vbc_data.end(),y.begin());

        ui->vbc_report->clearGraphs();
        ui->vbc_report->addGraph();
        ui->vbc_report->graph(0)->setLineStyle(QCPGraph::lsNone);
        ui->vbc_report->graph(0)->setScatterStyle(QCP::ScatterStyle(ui->scatter->value()));
        ui->vbc_report->graph(0)->setData(variables, y);
        float min_x = *std::min_element(variables.begin(),variables.end());
        float max_x = *std::max_element(variables.begin(),variables.end());
        float min_y = *std::min_element(vbc_data.begin(),vbc_data.end());
        float max_y = *std::max_element(vbc_data.begin(),vbc_data.end());

        ui->vbc_report->xAxis->setRange(min_x-(max_x-min_x)*0.1,
                                        max_x+(max_x-min_x)*0.1);
        ui->vbc_report->xAxis->setLabel(ui->foi->currentText());
        ui->vbc_report->yAxis->setRange(0,max_y+(max_y-min_y)*0.1);
        ui->vbc_report->yAxis->setLabel("QA");

    }
    else
    {
        for(unsigned int index = 0;index < vbc_data.size();++index)
            ui->subject_list->item(index,1)->setText(QString::number(vbc_data[index]));

        vbc_data.erase(std::remove(vbc_data.begin(),vbc_data.end(),0.0),vbc_data.end());
        float max_y = *std::max_element(vbc_data.begin(),vbc_data.end());
        std::vector<unsigned int> hist;
        image::histogram(vbc_data,hist,0,max_y,20);
        QVector<double> x(hist.size()+1),y(hist.size()+1);
        unsigned int max_hist = 0;
        for(unsigned int j = 0;j < hist.size();++j)
        {
            x[j] = max_y*(float)j/(float)hist.size();
            y[j] = hist[j];
            max_hist = std::max<unsigned int>(max_hist,hist[j]);
        }
        x.back() = max_y*(hist.size()+1)/hist.size();
        y.back() = 0;
        ui->vbc_report->clearGraphs();
        ui->vbc_report->addGraph();
        QPen pen;
        pen.setColor(QColor(20,20,100,200));
        ui->vbc_report->graph(0)->setLineStyle(QCPGraph::lsLine);
        ui->vbc_report->graph(0)->setPen(pen);
        ui->vbc_report->graph(0)->setData(x, y);
        ui->vbc_report->xAxis->setRange(0,x.back());
        ui->vbc_report->yAxis->setRange(0,max_hist);
    }

    ui->vbc_report->xAxis2->setVisible(true);
    ui->vbc_report->xAxis2->setTicks(false);
    ui->vbc_report->xAxis2->setTickLabels(false);
    ui->vbc_report->yAxis2->setVisible(true);
    ui->vbc_report->yAxis2->setTicks(false);
    ui->vbc_report->yAxis2->setTickLabels(false);

    ui->vbc_report->xAxis->setGrid(false);
    ui->vbc_report->yAxis->setGrid(false);
    ui->vbc_report->replot();

}

void vbc_dialog::on_y_pos_valueChanged(int arg1)
{
    on_x_pos_valueChanged(0);
}

void vbc_dialog::on_z_pos_valueChanged(int arg1)
{
    on_x_pos_valueChanged(0);
}

void vbc_dialog::on_scatter_valueChanged(int arg1)
{
    on_x_pos_valueChanged(0);
}

void vbc_dialog::on_save_report_clicked()
{
    QString filename = QFileDialog::getSaveFileName(
                this,
                "Save report as",
                work_dir + "/report.jpg",
                "JPEC file (*.jpg);;BMP file (*.bmp);;PDF file (*.pdf);;PNG file (*.png);;TXT file (*.txt);;All files (*)");
    if(QFileInfo(filename).completeSuffix().toLower() == "jpg")
        ui->vbc_report->saveJpg(filename,300,300,3);
    if(QFileInfo(filename).completeSuffix().toLower() == "bmp")
        ui->vbc_report->saveBmp(filename,300,300,3);
    if(QFileInfo(filename).completeSuffix().toLower() == "png")
        ui->vbc_report->savePng(filename,300,300,3);
    if(QFileInfo(filename).completeSuffix().toLower() == "pdf")
        ui->vbc_report->savePdf(filename,true,300,300);
    if(QFileInfo(filename).completeSuffix().toLower() == "txt")
        ui->vbc_report->saveTxt(filename);
}


void vbc_dialog::on_save_R2_clicked()
{
    QString filename = QFileDialog::getSaveFileName(
                this,
                "Save R2 values",
                db_file_name + ".R2.txt",
                "Report file (*.txt);;All files (*)");
    if(filename.isEmpty())
        return;
    std::ofstream out(filename.toLocal8Bit().begin());
    std::copy(vbc->handle->db.R2.begin(),vbc->handle->db.R2.end(),std::ostream_iterator<float>(out,"\n"));
}


void vbc_dialog::on_save_vector_clicked()
{
    QString filename = QFileDialog::getSaveFileName(
                this,
                "Save Vector",
                db_file_name + ".vec.mat",
                "Report file (*.mat);;All files (*)");
    if(filename.isEmpty())
        return;

    float threshold = ui->fp_coverage->value()*image::segmentation::otsu_threshold(
                image::make_image(vbc->handle->dir.fa[0],vbc->handle->dim));
    vbc->handle->db.save_subject_vector(filename.toLocal8Bit().begin(),fp_mask,threshold,ui->normalize_fp->isChecked());

}

void vbc_dialog::on_show_advanced_clicked()
{
    if(ui->advanced_options->isVisible())
        ui->advanced_options->hide();
    else
        ui->advanced_options->show();
}

void vbc_dialog::on_missing_data_checked_toggled(bool checked)
{
    ui->missing_value->setEnabled(checked);
}

void vbc_dialog::add_new_roi(QString name,QString source,std::vector<image::vector<3,short> >& new_roi)
{
    ui->roi_table->setRowCount(ui->roi_table->rowCount()+1);
    ui->roi_table->setItem(ui->roi_table->rowCount()-1,0,new QTableWidgetItem(name));
    ui->roi_table->setItem(ui->roi_table->rowCount()-1,1,new QTableWidgetItem(source));
    ui->roi_table->setItem(ui->roi_table->rowCount()-1,2,new QTableWidgetItem(QString::number(0)));
    ui->roi_table->openPersistentEditor(ui->roi_table->item(ui->roi_table->rowCount()-1,2));
    roi_list.push_back(new_roi);
}

void vbc_dialog::on_load_roi_from_atlas_clicked()
{
    std::auto_ptr<AtlasDialog> atlas_dialog(new AtlasDialog(this));
    if(atlas_dialog->exec() == QDialog::Accepted)
    {
        for(unsigned int i = 0;i < atlas_dialog->roi_list.size();++i)
        {
            std::vector<image::vector<3,short> > new_roi;
            unsigned short label = atlas_dialog->roi_list[i];
            for (image::pixel_index<3>index(vbc->handle->dim);index < vbc->handle->dim.size();++index)
            {
                image::vector<3> pos((const unsigned int*)(index.begin()));
                vbc->handle->subject2mni(pos);
                if(atlas_list[atlas_dialog->atlas_index].is_labeled_as(pos, label))
                    new_roi.push_back(image::vector<3,short>((const unsigned int*)index.begin()));
            }
            if(!new_roi.empty())
                add_new_roi(atlas_dialog->roi_name[i].c_str(),atlas_dialog->atlas_name.c_str(),new_roi);
        }
    }
}

void vbc_dialog::on_clear_all_roi_clicked()
{
    roi_list.clear();
    ui->roi_table->setRowCount(0);
}

void vbc_dialog::on_load_roi_from_file_clicked()
{
    QString file = QFileDialog::getOpenFileName(
                                this,
                                "Load ROI from file",
                                work_dir + "/roi.nii.gz",
                                "Report file (*.txt *.nii *nii.gz);;All files (*)");
    if(file.isEmpty())
        return;
    image::basic_image<short,3> I;
    image::matrix<4,4,float> transform;
    gz_nifti nii;
    if(!nii.load_from_file(file.toLocal8Bit().begin()))
    {
        QMessageBox::information(this,"Error","Invalid nifti file format",0);
        return;
    }
    nii >> I;
    transform.identity();
    nii.get_image_transformation(transform.begin());
    transform.inv();
    std::vector<image::vector<3,short> > new_roi;
    for (image::pixel_index<3> index(vbc->handle->dim);index < vbc->handle->dim.size();++index)
    {
        image::vector<3> pos(index);
        vbc->handle->subject2mni(pos);
        pos.to(transform);
        pos += 0.5;
        pos.floor();
        if(!I.geometry().is_valid(pos) || I.at(pos[0],pos[1],pos[2]) == 0)
            continue;
        new_roi.push_back(image::vector<3,short>((const unsigned int*)index.begin()));
    }
    if(new_roi.empty())
    {
        QMessageBox::information(this,"Error","The nifti contain no voxel with value greater than 0.",0);
        return;
    }
    add_new_roi(QFileInfo(file).baseName(),"Local File",new_roi);
}

void vbc_dialog::on_calculate_dif_clicked()
{
    float threshold = ui->fp_coverage->value()*image::segmentation::otsu_threshold(
                image::make_image(vbc->handle->dir.fa[0],vbc->handle->dim));
    vbc->handle->db.get_dif_matrix(fp_matrix,fp_mask,threshold,ui->normalize_fp->isChecked());
    fp_max_value = *std::max_element(fp_matrix.begin(),fp_matrix.end());
    fp_dif_map.resize(image::geometry<2>(vbc->handle->db.num_subjects,vbc->handle->db.num_subjects));
    for(unsigned int index = 0;index < fp_matrix.size();++index)
        fp_dif_map[index] = color_map[fp_matrix[index]*256.0/fp_max_value];
    on_fp_zoom_valueChanged(ui->fp_zoom->value());
}

void vbc_dialog::on_fp_zoom_valueChanged(double arg1)
{
    QImage qimage((unsigned char*)&*fp_dif_map.begin(),fp_dif_map.width(),fp_dif_map.height(),QImage::Format_RGB32);
    fp_dif_image = qimage.scaled(fp_dif_map.width()*ui->fp_zoom->value(),fp_dif_map.height()*ui->fp_zoom->value());
    fp_dif_scene.setSceneRect(0, 0, fp_dif_image.width()+80,fp_dif_image.height()+10);
    fp_dif_scene.clear();
    fp_dif_scene.setItemIndexMethod(QGraphicsScene::NoIndex);
    fp_dif_scene.addRect(0, 0, fp_dif_image.width(),fp_dif_image.height(),QPen(),fp_dif_image);

    QImage qbar((unsigned char*)&*color_bar.begin(),color_bar.width(),color_bar.height(),QImage::Format_RGB32);
    qbar = qbar.scaledToHeight(fp_dif_image.height());
    fp_dif_scene.addPixmap(QPixmap::fromImage(qbar))->moveBy(fp_dif_image.width()+10,0);
    fp_dif_scene.addText(QString::number(fp_max_value))->moveBy(fp_dif_image.width()+qbar.width()+10,-10);
    fp_dif_scene.addText(QString("0"))->moveBy(fp_dif_image.width()+qbar.width()+10,(int)fp_dif_image.height()-10);
}

void vbc_dialog::on_subject_view_tabBarClicked(int index)
{
    if(index == 1 && fp_dif_map.empty() )
        on_calculate_dif_clicked();
}

void vbc_dialog::on_save_dif_clicked()
{
    QString filename = QFileDialog::getSaveFileName(
                this,
                "Save Vector",
                db_file_name + ".vec.dif.mat",
                "MATLAB file (*.mat);;Figures (*.jpg *.png *.tif *.bmp;;All files (*)");
    if(filename.isEmpty())
        return;
    if(fp_matrix.empty() || fp_matrix.size() != vbc->handle->db.num_subjects*vbc->handle->db.num_subjects)
        on_calculate_dif_clicked();
    if(QFileInfo(filename).suffix().toLower() == "mat")
    {
        image::io::mat_write out(filename.toStdString().c_str());
        if(!out)
            return;
        out.write("dif",(const float*)&*fp_matrix.begin(),vbc->handle->db.num_subjects,vbc->handle->db.num_subjects);
    }
    else
    {
        QImage img(fp_dif_scene.sceneRect().size().toSize(), QImage::Format_RGB32);
        QPainter painter(&img);
        painter.fillRect(fp_dif_scene.sceneRect(),Qt::white);
        fp_dif_scene.render(&painter);
        img.save(filename);
    }
}


void vbc_dialog::on_add_db_clicked()
{
    QString filename = QFileDialog::getOpenFileName(
                           this,
                           "Open Database files",
                           db_file_name,
                           "Database files (*db?fib.gz);;All files (*)");
    if (filename.isEmpty())
        return;
    std::shared_ptr<fib_data> handle(new fib_data);
    begin_prog("reading connectometry db");
    if(!handle->load_from_file(filename.toStdString().c_str()))
    {
        QMessageBox::information(this,"Error",handle->error_msg.c_str(),0);
        return;
    }
    if(!vbc->handle->db.add_db(handle->db))
    {
        QMessageBox::information(this,"Error",vbc->handle->error_msg.c_str(),0);
        return;
    }
    update_subject_list();
    if(model.get())
    {
        model.reset(0);
        ui->subject_demo->clear();
        ui->run->setEnabled(false);
    }
    if(!fp_dif_map.empty())
        on_calculate_dif_clicked();
}


void vbc_dialog::on_view_x_toggled(bool checked)
{
    if(!checked)
        return;
    unsigned char dim = ui->view_x->isChecked() ? 0:(ui->view_y->isChecked() ? 1:2);
    ui->slice_pos->setMaximum(vbc->handle->dim[dim]-1);
    ui->slice_pos->setMinimum(0);
    ui->slice_pos->setValue(vbc->handle->dim[dim] >> 1);
}

void vbc_dialog::on_load_fp_mask_clicked()
{
    QString file = QFileDialog::getOpenFileName(
                                this,
                                "Load fingerprint mask from file",
                                work_dir,
                                "Report file (*.txt *.nii *nii.gz);;All files (*)");
    if(file.isEmpty())
        return;
    image::basic_image<float,3> I;
    gz_nifti nii;
    if(!nii.load_from_file(file.toLocal8Bit().begin()))
    {
        QMessageBox::information(this,"Error","Invalid nifti file format",0);
        return;
    }
    nii.toLPS(I);
    if(I.geometry() != fp_mask.geometry())
    {
        QMessageBox::information(this,"Error","Inconsistent image dimension. Please use DSI Studio to output the mask.",0);
        return;
    }
    for(unsigned int i = 0;i < I.size();++i)
        fp_mask[i] = I[i] ? 1:0;
    on_calculate_dif_clicked();
}

void vbc_dialog::on_save_fp_mask_clicked()
{
    QString FileName = QFileDialog::getSaveFileName(
                                this,
                                "Save fingerprint mask",
                                work_dir + "/mask.nii.gz",
                                "Report file (*.txt *.nii *nii.gz);;All files (*)");
    if(FileName.isEmpty())
        return;
    float fiber_threshold = ui->fp_coverage->value()*image::segmentation::otsu_threshold(
                image::make_image(vbc->handle->dir.fa[0],vbc->handle->dim));
    image::basic_image<float,3> mask(fp_mask);
    for(unsigned int index = 0;index < mask.size();++index)
        if(vbc->handle->dir.fa[0][index] < fiber_threshold)
            mask[index] = 0;
    gz_nifti file;
    file.set_voxel_size(vbc->handle->vs);
    file.set_image_transformation(vbc->handle->trans_to_mni.begin());
    file << mask;
    file.save_to_file(FileName.toLocal8Bit().begin());
}
