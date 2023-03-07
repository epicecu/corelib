#ifndef FUNCTION_H
#define FUNCTION_H

namespace corelib {

/**
 * Function
 * @brief Function base class
 */
class Function{
public:
    /// Called during setup 
    void initialise() {
        this->performInitialise();
    }

    /// Called by the main loop
    void iterate() {
        this->performIterate();
    }


protected:
    /// Called during setup
    virtual void performInitialise() = 0;

    /// Called every x Hz selected by setting the variable
    virtual void performIterate() = 0;

};
} // NAMESPACE
#endif // FUNCTION_H
