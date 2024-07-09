#include <rclcpp/rclcpp.hpp>
#include <memory>
#include <Eigen/Core>
#include <cv_bridge/cv_bridge.h>
#include "scene_flow_visualization.h" // Incluye el archivo .h proporcionado
#include <pd_flow_msgs/msg/combined_image.hpp>
#include <pd_flow_msgs/msg/flow_field.hpp>

using namespace std;
const unsigned int rows = 480; // Ajustar según tu imagen
const unsigned int cols = 640; // Ajustar según tu imagen
bool initialized = false;
int cont = 0;
class PDFlowNode : public rclcpp::Node
{

public:
    PDFlowNode() : Node("PD_flow_node"), pd_flow_(1, 30, 480)
    {
        cout << "Initializing PD_flow" << endl;

        // pd_flow_.initializePDFlow();
        subscription_ = this->create_subscription<pd_flow_msgs::msg::CombinedImage>(
            "/combined_image", 10, std::bind(&PDFlowNode::topic_callback, this, std::placeholders::_1));

        flow_pub_ = this->create_publisher<pd_flow_msgs::msg::FlowField>("flow_field", 10);
    }

private:
    void topic_callback(const pd_flow_msgs::msg::CombinedImage::SharedPtr msg)
    {
        try
        {
            // Convertir el mensaje de ROS a imágenes OpenCV
            rgb_image_ = cv_bridge::toCvCopy(msg->rgb_image, sensor_msgs::image_encodings::RGB8)->image;
            depth_image_ = cv_bridge::toCvCopy(msg->depth_image, sensor_msgs::image_encodings::TYPE_16UC1)->image;
        }
        catch (cv_bridge::Exception &e)
        {
            RCLCPP_ERROR(this->get_logger(), "cv_bridge exception: %s", e.what());
            return;
        }
        process_images();
    }

    void process_images()
    {
        if (cont == 0)
        {
            RCLCPP_INFO(this->get_logger(), "Iniciando el flujo flujo óptico cogiendo 2 imagenes iniciales...");
            pd_flow_.initializePDFlow();
            pd_flow_.process_frame(rgb_image_, depth_image_);
            pd_flow_.createImagePyramidGPU();
        }
        else if (cont == 1)
        {
            pd_flow_.process_frame(rgb_image_, depth_image_);
            pd_flow_.createImagePyramidGPU();
            pd_flow_.solveSceneFlowGPU();
        }
        else
        {
            RCLCPP_INFO(this->get_logger(), "Calculando flujo óptico...");
            // Pasar las imágenes a PD_flow
            pd_flow_.process_frame(rgb_image_, depth_image_);
            RCLCPP_INFO(this->get_logger(), "Comenza a calcular flujo óptico...");
            pd_flow_.createImagePyramidGPU();
            RCLCPP_INFO(this->get_logger(), "Piramide calculada con exito");
            pd_flow_.solveSceneFlowGPU();
            RCLCPP_INFO(this->get_logger(), "Flujo óptico calculado con exito");

            // Publicar los resultados
            pd_flow_.updateScene();
        }
        cont++;
        // Reiniciar las imágenes después de procesarlas
        rgb_image_ = cv::Mat();
        depth_image_ = cv::Mat();
    }

    
    void publish_flow_field()
    {
        auto msg = pd_flow_msgs::msg::FlowField();
        msg.header.stamp = this->get_clock()->now();
        msg.header.frame_id = "camera_frame";

        // Aplanar las matrices de movimiento y copiar los datos
        size_t num_elements = pd_flow_.dx[0].size();
        msg.dx.resize(num_elements);
        msg.dy.resize(num_elements);
        msg.dz.resize(num_elements);

        for (size_t i = 0; i < num_elements; ++i)
        {
            msg.dx[i] = pd_flow_.dx[0](i);
            msg.dy[i] = pd_flow_.dy[0](i);
            msg.dz[i] = pd_flow_.dz[0](i);
        }

        std::cout << "Publicando flujo óptico datos: " << msg.dx.size() << " elementos" << std::endl;
        flow_pub_->publish(msg);

        // Crear imagen OpenCV del flujo óptico
        cv::Mat flow_image = createImage();

        // Mostrar imagen con OpenCV
        cv::imshow("Optical Flow", flow_image);
        cv::waitKey(1); // Esperar un milisegundo para que se actualice la ventana
    }

    cv::Mat createImage() const
    {
        // Crear imagen RGB (una color por dirección)
        cv::Mat sf_image(rows, cols, CV_8UC3);

        // Calcular los valores máximos del flujo (de sus componentes)
        float maxmodx = 0.f, maxmody = 0.f, maxmodz = 0.f;
        for (unsigned int v = 0; v < rows; v++)
        {
            for (unsigned int u = 0; u < cols; u++)
            {
                size_t index = v + u * rows;
                if (fabs(pd_flow_.dx[0](index)) > maxmodx)
                    maxmodx = fabs(pd_flow_.dx[0](index));
                if (fabs(pd_flow_.dy[0](index)) > maxmody)
                    maxmody = fabs(pd_flow_.dy[0](index));
                if (fabs(pd_flow_.dz[0](index)) > maxmodz)
                    maxmodz = fabs(pd_flow_.dz[0](index));
            }
        }

        // Crear una representación RGB del flujo óptico
        for (unsigned int v = 0; v < rows; v++)
        {
            for (unsigned int u = 0; u < cols; u++)
            {
                size_t index = v + u * rows;
                sf_image.at<cv::Vec3b>(v, u)[0] = static_cast<unsigned char>(255.f * fabs(pd_flow_.dx[0](index)) / maxmodx); // Azul - x
                sf_image.at<cv::Vec3b>(v, u)[1] = static_cast<unsigned char>(255.f * fabs(pd_flow_.dy[0](index)) / maxmody); // Verde - y
                sf_image.at<cv::Vec3b>(v, u)[2] = static_cast<unsigned char>(255.f * fabs(pd_flow_.dz[0](index)) / maxmodz); // Rojo - z
            }
        }

        return sf_image;
    }

    rclcpp::Subscription<pd_flow_msgs::msg::CombinedImage>::SharedPtr subscription_;
    rclcpp::Publisher<pd_flow_msgs::msg::FlowField>::SharedPtr flow_pub_;
    PD_flow pd_flow_;
    cv::Mat rgb_image_;
    cv::Mat depth_image_;
};

int main(int argc, char *argv[])
{
    rclcpp::init(argc, argv);
    auto node = std::make_shared<PDFlowNode>();

    rclcpp::spin(node);

    rclcpp::shutdown();
    return 0;
}
