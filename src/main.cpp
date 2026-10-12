#include <Geode/Geode.hpp>
#include <Geode/modify/EditLevelLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

// ---------------------------------------------------------------
// Guardado / lectura de progreso
// Se combina el porcentaje REAL que ya guarda el juego en el nivel
// (m_normalPercent / m_practicePercent) con el que guarda este mod.
// ---------------------------------------------------------------
static std::string progressKey(GJGameLevel* level, bool practice) {
    return fmt::format(
        "lvl_{}_{}_{}",
        level->m_levelID.value(),
        std::string(level->m_levelName),
        practice ? "p" : "n"
    );
}

static int loadProgress(GJGameLevel* level, bool practice) {
    int saved = Mod::get()->getSavedValue<int>(progressKey(level, practice), 0);
    int game = practice
        ? static_cast<int>(level->m_practicePercent)
        : static_cast<int>(level->m_normalPercent.value());
    return std::clamp(std::max(saved, game), 0, 100);
}

static void storeProgress(GJGameLevel* level, bool practice, int pct) {
    auto key = progressKey(level, practice);
    auto mod = Mod::get();
    if (pct > mod->getSavedValue<int>(key, 0)) {
        mod->setSavedValue<int>(key, std::clamp(pct, 0, 100));
    }
}

// ---------------------------------------------------------------
// Una fila: título a la izquierda + barra (assets originales del juego:
// GJ_progressBar_001.png y bigFont.fnt). Las posiciones son relativas
// a la esquina inferior izquierda de "root".
// ---------------------------------------------------------------
static void addRow(
    CCNode* root, const char* title, int pct, ccColor3B color,
    CCSize barSize, float barLeft, float y, float labelRight
) {
    float midY = y + barSize.height / 2.f;
    float barMidX = barLeft + barSize.width / 2.f;

    // Título ("Normal Mode" / "Practice Mode"), alineado a la derecha, pegado a la barra
    auto titleLabel = CCLabelBMFont::create(title, "bigFont.fnt");
    titleLabel->setScale(0.5f);
    titleLabel->setAnchorPoint({1.f, 0.5f});
    titleLabel->setPosition({labelRight, midY});
    root->addChild(titleLabel, 3);

    // Fondo de la barra: textura original, oscura y semitransparente
    auto back = CCSprite::create("GJ_progressBar_001.png");
    if (back) {
        back->setColor({0, 0, 0});
        back->setOpacity(125);
        back->setPosition({barMidX, midY});
        root->addChild(back, 1);
    }

    // Relleno: misma textura, coloreada y recortada según el porcentaje.
    // OJO: setTextureRect usa PUNTOS (igual que getContentSize), no píxeles.
    if (pct > 0) {
        auto fill = CCSprite::create("GJ_progressBar_001.png");
        if (fill) {
            fill->setColor(color);
            fill->setAnchorPoint({0.f, 0.5f});
            fill->setTextureRect(CCRect(0.f, 0.f, barSize.width * pct / 100.f, barSize.height));
            fill->setScaleX(0.992f);
            fill->setScaleY(0.86f);
            fill->setPosition({barLeft + barSize.width * (1.f - 0.992f) / 2.f, midY});
            root->addChild(fill, 2);
        }
    }

    // Porcentaje centrado dentro de la barra
    if (Mod::get()->getSettingValue<bool>("show-text")) {
        auto percent = CCLabelBMFont::create(fmt::format("{}%", pct).c_str(), "bigFont.fnt");
        percent->setScale(0.5f);
        percent->setPosition({barMidX, midY});
        root->addChild(percent, 4);
    }
}

// Dos filas apiladas: Normal Mode (arriba) y Practice Mode (abajo)
static CCNode* buildProgressBar(int normal, int practice) {
    auto probe = CCSprite::create("GJ_progressBar_001.png");
    CCSize barSize = probe ? probe->getContentSize() : CCSize(220.f, 20.f);

    // Ancho del título más largo para alinear ambas barras
    auto t1 = CCLabelBMFont::create("Normal Mode", "bigFont.fnt");
    auto t2 = CCLabelBMFont::create("Practice Mode", "bigFont.fnt");
    float labelW = std::max(t1->getContentSize().width, t2->getContentSize().width) * 0.5f;

    constexpr float labelGap = 8.f;
    constexpr float rowGap = 4.f;
    float barLeft = labelW + labelGap;

    auto root = CCNode::create();
    root->setAnchorPoint({0.5f, 0.5f});
    root->setContentSize({barLeft + barSize.width, barSize.height * 2.f + rowGap});

    addRow(root, "Normal Mode", normal, {0, 255, 0}, barSize, barLeft, barSize.height + rowGap, labelW);
    addRow(root, "Practice Mode", practice, {0, 255, 255}, barSize, barLeft, 0.f, labelW);
    return root;
}

// ---------------------------------------------------------------
// Pantalla del nivel (editar / jugar / compartir): mostrar las barras
// ---------------------------------------------------------------
class $modify(ProgressEditLevelLayer, EditLevelLayer) {
    bool init(GJGameLevel* level) {
        if (!EditLevelLayer::init(level)) return false;

        auto winSize = CCDirector::get()->getWinSize();
        auto bar = buildProgressBar(loadProgress(level, false), loadProgress(level, true));
        bar->setID("progress-bar"_spr);

        float userScale = static_cast<float>(Mod::get()->getSettingValue<double>("bar-scale"));
        float width = std::max(bar->getContentSize().width, 1.f);
        bar->setScale(userScale * 270.f / width); // ancho total ~270 unidades

        // Posición original: centrado, justo debajo de los 3 botones
        bar->setPosition({winSize.width / 2.f, winSize.height * 0.26f});
        this->addChild(bar, 10);

        return true;
    }
};

// ---------------------------------------------------------------
// Al jugar el nivel: guardar el mejor porcentaje (normal / práctica)
// ---------------------------------------------------------------
class $modify(ProgressPlayLayer, PlayLayer) {
    struct Fields {
        int m_lastPct = -1;
        bool m_lastPractice = false;
    };

    bool isLocalLevel() {
        return m_level && m_level->m_levelType == GJLevelType::Editor;
    }

    void updateProgressbar() {
        PlayLayer::updateProgressbar();
        if (!this->isLocalLevel()) return;

        int pct = std::clamp(static_cast<int>(this->getCurrentPercent()), 0, 100);
        bool practice = m_isPracticeMode;

        if (pct != m_fields->m_lastPct || practice != m_fields->m_lastPractice) {
            m_fields->m_lastPct = pct;
            m_fields->m_lastPractice = practice;
            storeProgress(m_level, practice, pct);
        }
    }

    void levelComplete() {
        PlayLayer::levelComplete();
        if (!this->isLocalLevel()) return;
        storeProgress(m_level, m_isPracticeMode, 100);
        (void) Mod::get()->saveData();
    }

    void onQuit() {
        PlayLayer::onQuit();
        if (this->isLocalLevel()) (void) Mod::get()->saveData();
    }
};
