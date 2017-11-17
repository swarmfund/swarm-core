#find all *.x
file(GLOB SRC_X_FILES ${XDRS_PATH}/*.x)

set(adad)
#iterate all *.x
foreach(X_FILE ${SRC_X_FILES})
    #get only name (Stellar-ladger without .x)
    get_filename_component(X_NAME ${X_FILE} NAME_WE)
    #set output file (<XDRS_PATH>/Stellar-ladger.h)
    set(H_GENERATE_FILE ${XDRS_PATH}/${X_NAME}.h)
    #variable H_GENERATE_FILES is like list<File>. And following we add(new File())
    set(H_GENERATE_FILES ${H_GENERATE_FILES} ${H_GENERATE_FILE})
    #Track .x files. For each .x file create command. And command will be executed if file changes.
    add_custom_command(OUTPUT ${H_GENERATE_FILE}
            COMMAND xdrc -hh -o ${X_NAME}.h ${X_NAME}.x
            MAIN_DEPENDENCY ${X_FILE}
            WORKING_DIRECTORY ${XDRS_PATH}
            )
endforeach()