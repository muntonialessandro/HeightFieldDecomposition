#include "dcel_face.h"
#include "dcel_face_iterators.h"
#include "dcel_half_edge.h"

/*************************
 * Public Inline Methods *
 *************************/
/**
 * \~Italian
 * @brief Restituisce l'id identificativo nella Dcel della faccia
 * @return L'id della faccia
 */
inline unsigned int Dcel::Face::getId() const {
    return id;
}

inline const Dcel::Vertex*Dcel::Face::getVertex1() const {
    return outerHalfEdge->getFromVertex();
}

inline const Dcel::Vertex*Dcel::Face::getVertex2() const {
    return outerHalfEdge->getToVertex();
}

inline const Dcel::Vertex*Dcel::Face::getVertex3() const {
    return outerHalfEdge->getNext()->getToVertex();
}

/**
 * \~Italian
 * @brief Restituisce il flag associato alla faccia
 * @return Il flag della faccia
 */
inline int Dcel::Face::getFlag() const {
    return flag;
}

/**
 * \~Italian
 * @brief Restituisce il vettore normale alla faccia
 * @note Non ricalcola la normale, restituisce solo l'ultima normale calcolata o settata
 * @return La normale della faccia
 */
inline Vec3 Dcel::Face::getNormal() const {
    return normal;
}

/**
 * \~Italian
 * @brief Restituisce la superficie della faccia
 * @note Non ricalcola l'area, restituisce solo l'ultima area calcolata o settata
 * @return L'area della faccia
 */
inline double Dcel::Face::getArea() const {
    return area;
}

/**
 * \~Italian
 * @brief Restituisce il colore associato alla faccia
 * @return Il colore della faccia
 */
inline QColor Dcel::Face::getColor() const {
    return color;
}

/**
 * \~Italian
 * @brief Restituisce il puntatore all'half edge costante di bordo esterno della faccia
 * @return L'HalfEdge di bordo della faccia
 */
inline const Dcel::HalfEdge* Dcel::Face::getOuterHalfEdge() const {
    return outerHalfEdge;
}

/**
 * \~Italian
 * @brief Restituisce il numero di inner half edges contenuti nella faccia, ossia il numero di buchi
 * @return Il numero di HalfEdge interni della faccia
 */
inline int Dcel::Face::getNumberInnerHalfEdges() const {
    return innerHalfEdges.size();
}

/**
 * \~Italian
 * @brief Restituisce true se la faccia contiene buchi
 * @return True se la faccia contiene buchi, false altrimenti
 */
inline bool Dcel::Face::hasHoles() const {
    return (innerHalfEdges.size() != 0);
}

/**
 * \~Italian
 * @brief Operatore di uguaglianza tra facce
 * @param[in] otherFace: faccia con cui verrà verificata l'uguaglianza con la faccia this
 * @return True se le facce sono uguali, false altrimenti
 * @todo Da riscrivere
 */
inline bool Dcel::Face::operator == (const Face& otherFace) const {
    if (otherFace.id == id) return true;
    else return false;
}

/**
 * \~Italian
 * @brief Operatore di disuguaglianza tra facce
 * @param[in] otherFace: faccia con cui verrà verificata la disuguaglianza con la faccia this
 * @return True se le facce sono diverse, false altrimenti
 */
inline bool Dcel::Face::operator != (const Face& otherFace) const {
    return !(*this == otherFace);
}

/**
 * \~Italian
 * @brief Restituisce true se l'outer half edge non è null
 */
inline bool Dcel::Face::checkOuterHalfEdge() const {
    return outerHalfEdge != nullptr;
}

inline Dcel::Vertex*Dcel::Face::getVertex1() {
    return outerHalfEdge->getFromVertex();
}

inline Dcel::Vertex*Dcel::Face::getVertex2() {
    return outerHalfEdge->getToVertex();
}

inline Dcel::Vertex*Dcel::Face::getVertex3() {
    return outerHalfEdge->getNext()->getToVertex();
}

/**
 * \~Italian
 * @brief Setta il flag della faccia a 1
 */
inline void Dcel::Face::setFlag() {
    flag = 1;
}

/**
 * \~Italian
 * @brief Setta il flag della faccia
 * @param[in] newFlag: il valore del flag che verrà settato
 */
inline void Dcel::Face::setFlag(int newFlag) {
    flag = newFlag;
}

/**
 * \~Italian
 * @brief Setta il flag della faccia a 0
 */
inline void Dcel::Face::resetFlag() {
    flag = 0;
}

/**
 * \~Italian
 * @brief Setta il vettore normale della faccia
 * @param[in] newNormal: il vettore normale che verrà settato
 */
inline void Dcel::Face::setNormal(const Vec3& newNormal) {
    normal = newNormal;
}

/**
 * \~Italian
 * @brief Setta la superficie della faccia
 * @param[in] newArea: il valore dell'area che verrà settato
 */
inline void Dcel::Face::setArea(double newArea) {
    area = newArea;
}

/**
 * \~Italian
 * @brief Assegna un nuovo colore alla faccia
 * @param[in] newColor: il nuovo colore che verrà assegnato alla faccia
 */
inline void Dcel::Face::setColor(const QColor& newColor) {
    color = newColor;
}

/**
 * \~Italian
 * @brief Restituisce il puntatore all'half edge di bordo esterno della faccia
 * @return L'HalfEdge di bordo della faccia
 */
inline Dcel::HalfEdge* Dcel::Face::getOuterHalfEdge() {
    return outerHalfEdge;
}

/**
 * \~Italian
 * @brief Assegna un nuovo half edge di bordo esterno alla faccia
 * @param[in] newOuterHalfEdge: puntatore all'half edge di bordo esterno assegnato alla faccia
 */
inline void Dcel::Face::setOuterHalfEdge(Dcel::HalfEdge* newOuterHalfEdge) {
    outerHalfEdge = newOuterHalfEdge;
}

/**
 * \~Italian
 * @brief Aggiunge un nuovo half edge di bordo interno (ossia un buco) alla faccia
 * @param[in] newInnerHalfEdge: nuovo half edge di bordo interno aggiunto alla faccia
 */
inline void Dcel::Face::addInnerHalfEdge(Dcel::HalfEdge* newInnerHalfEdge) {
    innerHalfEdges.push_back(newInnerHalfEdge);
}

/**************************
 * Private Inline Methods *
 **************************/

/**
 * \~Italian
 * @brief Setta l'id della faccia.
 *
 * Questa funzione dovrebbe essere chiamata solamente dalla classe Dcel.
 *
 * @param[in] id: nuovo id che verrà assegnato alla faccia
 */
inline void Dcel::Face::setId(unsigned int id) {
    this->id = id;
}