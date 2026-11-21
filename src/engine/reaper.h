#pragma once
// Destrói objetos pesados (descodificadores com threads) numa thread à parte:
// parar uma thread a meio de descodificar pode demorar milissegundos, e isso
// nunca pode acontecer no render loop nem na thread de áudio.

#include <memory>

namespace vthost {

void retireLater(std::shared_ptr<void> object);

} // namespace vthost
